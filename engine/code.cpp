// code.cpp - EBOV-TIP within-host engine + TIP-profile screening (C++20, standard library only)
// Model: Equations 1-10 of the proposal (nine dynamic states + four running integrals).
// Build : g++ -std=c++20 -O2 -pthread -Wall -Wextra code.cpp -o engine
// Modes : ./engine [input.txt] [output.txt] [errors.txt]      single trajectory (+ output.svg plot)
//         ./engine --screen [screen.txt] [input.txt] [errors.txt]   TIP-profile screening
//         ./engine --selftest [input.txt]                      RK4 convergence-order check
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// 0..9 dynamic states (9 = P, TIP-only primed cells), 10..13 running integrals (JV, JC, JH, U) so objective weights can be applied afterwards
enum Idx { iT, iEW, iIW, iVW, iEC, iIC, iVT, iC, iH, iP, iJV, iJC, iJH, iU, N };
constexpr int kDyn = 10;
using State = std::array<double, N>;

struct Params {
  double lambda = 5e4, dT = 0.07, bW = 5e-7, bT = 5e-7, kW = 6, kCo = 6;
  double dW = 2, dCo = 2, pW = 500, pT = 1500, alpha = 0.2, cW = 10, cT = 10;
  double dN = 0.5, KF = 50, eps_ab = 0, eps_clr = 0, kappa = 1;
  double c_rest = 5, dC = 5, gamma = 3, KC = 50, rhoI = 0.005, rhoV = 1e-6;
  double sigma_v = 0.5, H0 = 70, Hmax = 130, dH = 50, KH = 100, n_hill = 2, tau_H = 0.02;
  double w1 = 1, w2 = 1, w3 = 1, w4 = 1e-6, C_crit = 500, H_crit = 130, v_ext = 2e-4;
  double primed = 1, rhoT = 0, ext_floor = 0, VW0 = 100, tip_dose = 0, tip_time = 0, t_end = 30, h = 0.005, dt_out = 0.25;
  double Ts = 0, Cs = 0, Hs = 0, s0 = 0;  // derived
};

static double hill(double x, double K, int n) {
  double a = 1, b = 1;
  for (int i = 0; i < n; ++i) { a *= x; b *= K; }
  return a / (b + a);
}
static double vagal(const Params& p, double H) {
  return 1.0 - p.sigma_v * std::clamp((p.Hmax - H) / (p.Hmax - p.H0), 0.0, 1.0);
}

static void rhs(const Params& p, const State& y, State& d) {
  const double ent = 1.0 - p.eps_ab;
  const double cyt = p.dN * y[iC] / (p.KF + y[iC]);
  const double inf = ent * p.bW * y[iT] * y[iVW];
  const double tip = ent * p.bT * y[iVT];
  const double supW = ent * p.bW * y[iVW];                                       // WT entry into a primed cell
  d[iT]  = p.lambda - p.dT * y[iT] - inf - tip * y[iT];                          // Eq 1
  d[iEW] = inf - p.kW * y[iEW] - tip * y[iEW];                                   // Eq 2
  d[iIW] = p.kW * y[iEW] - (p.dW + cyt) * y[iIW] - tip * y[iIW];                 // Eq 3
  d[iEC] = tip * y[iEW] + p.primed * supW * y[iP] - p.kCo * y[iEC];              // Eq 4 (+ superinfection of primed cells)
  d[iIC] = p.kCo * y[iEC] + tip * y[iIW] - (p.dCo + cyt) * y[iIC];               // Eq 5
  d[iP]  = p.primed * (tip * y[iT] - supW * y[iP]) - p.dT * y[iP];               // Eq 4b primed (TIP-only) cells
  d[iVW] = p.pW * y[iIW] + p.alpha * p.pW * y[iIC]
         - p.cW * (1.0 + p.kappa * p.eps_clr) * y[iVW];                          // Eq 6
  d[iVT] = p.pT * y[iIC] - p.cT * y[iVT];                                        // Eq 7
  d[iC]  = vagal(p, y[iH]) * (p.s0 + p.rhoI * (y[iIW] + y[iIC]) + p.rhoV * y[iVW] + p.rhoT * y[iVT]
         + p.gamma * hill(y[iC], p.KC, 2)) - p.dC * y[iC];                       // Eq 8-9
  d[iH]  = (p.H0 + p.dH * hill(y[iC], p.KH, (int)p.n_hill) - y[iH]) / p.tau_H;  // Eq 10
  const double ec = (y[iC] - p.Cs) / p.C_crit, eh = (y[iH] - p.Hs) / p.H_crit;
  d[iJV] = std::log10(1.0 + std::max(y[iVW], 0.0));                              // Eq 14 terms
  d[iJC] = ec * ec;
  d[iJH] = eh * eh;
  d[iU]  = y[iVT];
}

static void rk4(const Params& p, State& y, double h) {
  State k1, k2, k3, k4, s;
  rhs(p, y, k1);
  for (int i = 0; i < N; ++i) s[i] = y[i] + 0.5 * h * k1[i];
  rhs(p, s, k2);
  for (int i = 0; i < N; ++i) s[i] = y[i] + 0.5 * h * k2[i];
  rhs(p, s, k3);
  for (int i = 0; i < N; ++i) s[i] = y[i] + h * k3[i];
  rhs(p, s, k4);
  for (int i = 0; i < N; ++i) y[i] += (h / 6.0) * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
}

// infinity-norm of a finite-difference Jacobian of the dynamic block: bound on the spectral radius
static double jac_bound(const Params& p, const State& y) {
  State f0, f1, z = y;
  rhs(p, y, f0);
  std::array<double, kDyn> row{};
  for (int j = 0; j < kDyn; ++j) {
    const double e = 1e-6 * std::max(1.0, std::fabs(y[j]));
    z[j] = y[j] + e;
    rhs(p, z, f1);
    z[j] = y[j];
    for (int i = 0; i < kDyn; ++i) row[i] += std::fabs((f1[i] - f0[i]) / e);
  }
  return *std::max_element(row.begin(), row.end());
}

// ------------------------------------------------------------------ parameters
struct Entry { const char* name; double* ptr; };
static std::vector<Entry> registry(Params& p) {
  return {{"lambda", &p.lambda}, {"d_T", &p.dT}, {"beta_W", &p.bW}, {"beta_T", &p.bT},
    {"k_W", &p.kW}, {"k_Co", &p.kCo}, {"delta_W", &p.dW}, {"delta_Co", &p.dCo},
    {"p_W", &p.pW}, {"p_T", &p.pT}, {"alpha", &p.alpha}, {"c_W", &p.cW}, {"c_T", &p.cT},
    {"delta_N", &p.dN}, {"K_F", &p.KF}, {"eps_Ab", &p.eps_ab}, {"eps_Clr", &p.eps_clr},
    {"kappa", &p.kappa}, {"C_rest", &p.c_rest}, {"d_C", &p.dC}, {"gamma", &p.gamma},
    {"K_C", &p.KC}, {"rho_I", &p.rhoI}, {"rho_V", &p.rhoV}, {"rho_T", &p.rhoT}, {"primed", &p.primed}, {"sigma_v", &p.sigma_v},
    {"H_0", &p.H0}, {"H_max", &p.Hmax}, {"Delta_H", &p.dH}, {"K_H", &p.KH},
    {"n_hill", &p.n_hill}, {"tau_H", &p.tau_H}, {"w1", &p.w1}, {"w2", &p.w2},
    {"w3", &p.w3}, {"w4", &p.w4}, {"C_crit", &p.C_crit}, {"H_crit", &p.H_crit},
    {"V_ext", &p.v_ext}, {"ext_floor", &p.ext_floor}, {"V_W0", &p.VW0}, {"tip_dose", &p.tip_dose},
    {"tip_time", &p.tip_time}, {"t_end", &p.t_end}, {"h", &p.h}, {"dt_out", &p.dt_out}};
}

static std::string trim(std::string s) {
  s.erase(0, s.find_first_not_of(" \t\r")); s.erase(s.find_last_not_of(" \t\r") + 1); return s;
}
static std::string errs_line(int ln, const std::string& m) { return "ERROR: line " + std::to_string(ln) + ": " + m; }

static void load_stream(std::istream& in, Params& p, std::vector<std::string>& err) {
  auto reg = registry(p);
  std::string line; int ln = 0;
  while (std::getline(in, line)) {
    ++ln;
    if (auto c = line.find('#'); c != std::string::npos) line.erase(c);
    auto eq = line.find('=');
    if (eq == std::string::npos) {
      if (!trim(line).empty()) err.push_back(errs_line(ln, "expected 'name = value'"));
      continue;
    }
    const std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
    char* end = nullptr;
    const double v = std::strtod(val.c_str(), &end);
    if (val.empty() || *end != '\0' || !std::isfinite(v)) { err.push_back(errs_line(ln, "bad number for '" + key + "'")); continue; }
    bool found = false;
    for (auto& e : reg) if (key == e.name) { *e.ptr = v; found = true; }
    if (!found) err.push_back(errs_line(ln, "unknown parameter '" + key + "'"));
  }
}

[[maybe_unused]] static void load(const std::string& path, Params& p, std::vector<std::string>& err) {
  std::ifstream in(path);
  if (!in) { err.push_back("ERROR: cannot open " + path); return; }
  load_stream(in, p, err);
}

static void derive(Params& p) {
  p.Ts = p.lambda / p.dT;
  p.Cs = p.c_rest;
  p.Hs = p.H0 + p.dH * hill(p.Cs, p.KH, (int)p.n_hill);
  p.s0 = p.dC * p.Cs / vagal(p, p.Hs) - p.gamma * hill(p.Cs, p.KC, 2);
}

static void validate(Params& p, std::vector<std::string>& err) {
  auto pos = [&](const char* n, double v) { if (!(v > 0)) err.push_back(std::string("ERROR: ") + n + " must be > 0"); };
  pos("lambda", p.lambda); pos("d_T", p.dT); pos("beta_W", p.bW); pos("k_W", p.kW);
  pos("k_Co", p.kCo); pos("delta_W", p.dW); pos("delta_Co", p.dCo); pos("p_W", p.pW);
  pos("p_T", p.pT); pos("c_W", p.cW); pos("c_T", p.cT); pos("K_F", p.KF); pos("K_C", p.KC);
  pos("K_H", p.KH); pos("d_C", p.dC); pos("tau_H", p.tau_H); pos("C_rest", p.c_rest);
  pos("C_crit", p.C_crit); pos("H_crit", p.H_crit); pos("t_end", p.t_end); pos("h", p.h);
  pos("dt_out", p.dt_out);
  if (p.bT < 0 || p.dN < 0 || p.gamma < 0 || p.rhoI < 0 || p.rhoV < 0 || p.VW0 < 0 || p.tip_dose < 0)
    err.push_back("ERROR: beta_T, delta_N, gamma, rho_I, rho_V, V_W0, tip_dose must be >= 0");
  if (p.alpha < 0 || p.alpha > 1) err.push_back("ERROR: alpha must be in [0,1]");
  if (p.sigma_v < 0 || p.sigma_v >= 1) err.push_back("ERROR: sigma_v must be in [0,1)");
  if (p.eps_ab < 0 || p.eps_ab >= 1) err.push_back("ERROR: eps_Ab must be in [0,1)");
  if (p.eps_clr < 0 || p.eps_clr > 1) err.push_back("ERROR: eps_Clr must be in [0,1]");
  if (p.n_hill < 1 || p.n_hill > 4 || p.n_hill != std::floor(p.n_hill)) err.push_back("ERROR: n_hill must be an integer 1..4");
  if (p.ext_floor != 0 && p.ext_floor != 1) err.push_back("ERROR: ext_floor must be 0 or 1");
  if (p.Hmax <= p.H0) err.push_back("ERROR: H_max must exceed H_0");
  if (p.tip_time < 0 || p.tip_time > p.t_end) err.push_back("ERROR: tip_time must lie in [0, t_end]");
  if (p.dt_out < p.h) err.push_back("ERROR: dt_out must be >= h");
  if (!err.empty()) return;
  derive(p);
  if (p.s0 < 0) err.push_back("ERROR: C_rest too low for these parameters (basal synthesis s_0 would be negative)");
  if (3.0 * std::sqrt(3.0) / 8.0 * p.gamma / p.KC >= p.dC)
    err.push_back("WARNING: Eq 11 violated - cytokine subsystem may be bistable (storm-class regime)");
}

static double R0(const Params& p) {
  return p.bW * p.pW * p.Ts / (p.cW * (p.dW + p.dN * p.Cs / (p.KF + p.Cs)));  // Eq 12
}

// ------------------------------------------------------------------ one run
struct Traj { std::vector<double> t; std::vector<State> y; };
struct Result {
  double peakV = 0, tPeakV = 0, peakC = 0, peakH = 0, tExt = -1, Vfinal = 0;
  std::array<double, 4> I{};  // JV, JC, JH, U
  bool extinct = false, ok = true;
  int reduced = 0;
};
static double Jw(const Params& p, const Result& r) { return p.w1 * r.I[0] + p.w2 * r.I[1] + p.w3 * r.I[2] + p.w4 * r.I[3]; }

static Result run(const Params& p, Traj* tr, std::vector<std::string>& err) {
  Result r;
  State y{};
  y[iT] = p.Ts; y[iVW] = p.VW0; y[iC] = p.Cs; y[iH] = p.Hs;
  double t = 0, lastAbove = -1;
  bool dosed = false;
  auto dose_if_due = [&] { if (!dosed && t >= p.tip_time - 1e-12) { y[iVT] += p.tip_dose; dosed = true; } };
  auto track = [&] {
    if (y[iVW] > r.peakV) { r.peakV = y[iVW]; r.tPeakV = t; }
    r.peakC = std::max(r.peakC, y[iC]); r.peakH = std::max(r.peakH, y[iH]);
    if (y[iVW] >= p.v_ext) lastAbove = t;
  };
  auto advance = [&](double to) {
    const double rho = jac_bound(p, y);
    const double hmax = std::min(p.h, 2.5 / std::max(rho, 1e-12));  // pre-flight stability bound
    const int n = std::max(1, (int)std::ceil((to - t) / hmax - 1e-9));
    if (hmax < p.h) ++r.reduced;
    const double hs = (to - t) / n;
    for (int s = 0; s < n; ++s) {
      rk4(p, y, hs);
      t += hs;
      for (int i = 0; i < N; ++i)
        if (!std::isfinite(y[i]) || y[i] < -1e-6 * std::max(1.0, std::fabs(p.Ts))) {
          err.push_back("ERROR: invariant broken (non-finite or negative state) at t = " + std::to_string(t));
          r.ok = false; return;
        }
      if (p.ext_floor > 0.5 && y[iVW] < p.v_ext &&
          y[iEW] + y[iIW] + y[iEC] + y[iIC] < p.v_ext) {  // WT-free: less than one virion/cell per host
        y[iVW] = y[iEW] = y[iIW] = y[iEC] = y[iIC] = 0;
      }
      track();
    }
    t = to;
  };
  auto emit = [&] { if (tr) { tr->t.push_back(t); tr->y.push_back(y); } };
  dose_if_due(); track(); emit();
  const int nOut = (int)std::ceil(p.t_end / p.dt_out - 1e-9);
  for (int k = 1; k <= nOut && r.ok; ++k) {
    const double target = std::min(k * p.dt_out, p.t_end);
    if (!dosed && p.tip_time > t && p.tip_time < target) { advance(p.tip_time); dose_if_due(); }
    if (!r.ok) break;
    advance(target);
    dose_if_due();
    emit();
  }
  for (int i = 0; i < 4; ++i) r.I[i] = y[iJV + i];
  r.Vfinal = y[iVW];
  r.extinct = r.ok && y[iVW] < p.v_ext;
  if (r.extinct) r.tExt = std::max(lastAbove, 0.0);
  return r;
}

// ------------------------------------------------------------------ SVG plots (no dependencies)
struct Series { std::vector<double> x, y; std::string color; bool dashed = false; std::string name; };
static std::string fmt(double v) { char b[32]; std::snprintf(b, sizeof b, "%.3g", v); return b; }
static const double kNaN = std::numeric_limits<double>::quiet_NaN();

static std::string panel(double X, double Y, double W, double H, const std::string& title, const std::string& ylab,
                         const std::vector<Series>& ss, bool logy, double hline, const std::string& hlab, double xlo_ = kNaN) {
  const double L = 76, R = 14, T = 26, B = 34;
  double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
  auto ty = [&](double v) { return logy ? std::log10(std::max(v, 1e-4)) : v; };
  for (auto& s : ss) for (size_t i = 0; i < s.x.size(); ++i) {
    x0 = std::min(x0, s.x[i]); x1 = std::max(x1, s.x[i]); y0 = std::min(y0, ty(s.y[i])); y1 = std::max(y1, ty(s.y[i])); }
  if (!std::isnan(hline)) { y0 = std::min(y0, ty(hline)); y1 = std::max(y1, ty(hline)); }
  if (!std::isnan(xlo_)) x0 = xlo_;
  if (logy) { y0 = std::floor(y0); y1 = std::ceil(y1); if (y1 - y0 < 1) y1 = y0 + 1; }
  else { y0 = std::min(0.0, y0); y1 = y1 * 1.06 + 1e-12; }
  if (x1 <= x0) x1 = x0 + 1;
  const double pw = W - L - R, ph = H - T - B;
  auto px = [&](double v) { return X + L + (v - x0) / (x1 - x0) * pw; };
  auto py = [&](double v) { return Y + T + (1 - (ty(v) - y0) / (y1 - y0)) * ph; };
  auto pyr = [&](double raw) { return Y + T + (1 - (raw - y0) / (y1 - y0)) * ph; };
  std::ostringstream o;
  o << "<text x='" << X + L << "' y='" << Y + 16 << "' font-size='13' font-weight='bold'>" << title << "</text>\n";
  o << "<rect x='" << X + L << "' y='" << Y + T << "' width='" << pw << "' height='" << ph << "' fill='none' stroke='#888'/>\n";
  for (int k = 0; k <= 4; ++k) {
    const double fx = x0 + (x1 - x0) * k / 4.0, fy = y0 + (y1 - y0) * k / 4.0;
    o << "<line x1='" << px(fx) << "' y1='" << Y + T + ph << "' x2='" << px(fx) << "' y2='" << Y + T + ph + 4 << "' stroke='#888'/>"
      << "<text x='" << px(fx) << "' y='" << Y + T + ph + 17 << "' font-size='11' text-anchor='middle'>" << fmt(fx) << "</text>\n";
    o << "<line x1='" << X + L - 4 << "' y1='" << pyr(fy) << "' x2='" << X + L << "' y2='" << pyr(fy) << "' stroke='#888'/>"
      << "<text x='" << X + L - 7 << "' y='" << pyr(fy) + 4 << "' font-size='11' text-anchor='end'>"
      << (logy ? "1e" + fmt(fy) : fmt(fy)) << "</text>\n";
  }
  o << "<text x='" << X + L + pw / 2 << "' y='" << Y + H - 4 << "' font-size='11' text-anchor='middle'>time (days)</text>\n";
  o << "<text x='" << X + 12 << "' y='" << Y + T + ph / 2 << "' font-size='11' text-anchor='middle' transform='rotate(-90 " << X + 12 << " " << Y + T + ph / 2 << ")'>" << ylab << "</text>\n";
  if (!std::isnan(hline)) {
    o << "<line x1='" << X + L << "' y1='" << py(hline) << "' x2='" << X + L + pw << "' y2='" << py(hline)
      << "' stroke='#A32D2D' stroke-dasharray='2 3'/><text x='" << X + L + pw - 3 << "' y='" << py(hline) - 4
      << "' font-size='11' text-anchor='end' fill='#A32D2D'>" << hlab << "</text>\n";
  }
  for (auto& s : ss) {
    o << "<polyline fill='none' stroke='" << s.color << "' stroke-width='1.8'" << (s.dashed ? " stroke-dasharray='6 4'" : "") << " points='";
    for (size_t i = 0; i < s.x.size(); ++i) o << px(s.x[i]) << "," << py(s.y[i]) << " ";
    o << "'/>\n";
  }
  return o.str();
}

static void legend(std::ostringstream& o, double x, double y, const std::vector<std::pair<std::string, std::pair<std::string, bool>>>& items) {
  for (auto& it : items) {
    o << "<line x1='" << x << "' y1='" << y << "' x2='" << x + 26 << "' y2='" << y << "' stroke='" << it.second.first
      << "' stroke-width='2'" << (it.second.second ? " stroke-dasharray='6 4'" : "") << "/><text x='" << x + 32 << "' y='" << y + 4
      << "' font-size='12'>" << it.first << "</text>\n";
    x += 40 + 7.2 * it.first.size();
  }
}

static Series col(const Traj& t, int idx, const std::string& c, bool dashed, const std::string& name, int idx2 = -1) {
  Series s; s.color = c; s.dashed = dashed; s.name = name; s.x = t.t;
  for (auto& v : t.y) s.y.push_back(v[idx] + (idx2 >= 0 ? v[idx2] : 0.0));
  return s;
}

static std::string traj_svg_str(const std::string& title, const Params& p, const Traj& a, const Traj* b) {
  std::ostringstream o;
  const double W = 440, H = 250;
  o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << 2 * W + 20 << "' height='" << 2 * H + 70
    << "' viewBox='0 0 " << 2 * W + 20 << " " << 2 * H + 70 << "' font-family='Arial,Helvetica,sans-serif'>\n<rect width='100%' height='100%' fill='white'/>\n";
  o << "<text x='10' y='20' font-size='15' font-weight='bold'>" << title << "</text>\n";
  const std::string blue = "#185FA5", coral = "#D85A30", grey = "#888780";
  { std::vector<Series> s{col(a, iVW, blue, false, "V_W")};
    s.push_back(col(a, iVT, coral, false, "V_T"));
    if (b) s.push_back(col(*b, iVW, grey, true, "V_W untreated"));
    o << panel(10, 30, W, H, "Free virus (copies/mL, log)", "log10 copies/mL", s, true, p.v_ext, "V_ext"); }
  { std::vector<Series> s{col(a, iIW, blue, false, "I_W"), col(a, iIC, coral, false, "I_Co")};
    if (b) s.push_back(col(*b, iIW, grey, true, "I_W untreated"));
    o << panel(20 + W, 30, W, H, "Productively infected cells", "cells/mL", s, false, kNaN, ""); }
  { std::vector<Series> s{col(a, iC, blue, false, "C")};
    if (b) s.push_back(col(*b, iC, grey, true, "C untreated"));
    o << panel(10, 30 + H, W, H, "Cytokine (IL-6-like)", "pg/mL", s, false, p.C_crit, "C_crit"); }
  { std::vector<Series> s{col(a, iH, blue, false, "H")};
    if (b) s.push_back(col(*b, iH, grey, true, "H untreated"));
    o << panel(20 + W, 30 + H, W, H, "Pulse rate", "beats/min", s, false, p.H_crit, "H_crit"); }
  legend(o, 14, 2 * H + 52, {{"with TIP", {blue, false}}, {"TIP virions V_T / I_Co", {coral, false}}, {"untreated", {grey, true}}});
  o << "</svg>\n";
  return o.str();
}
static void write_traj_svg(const std::string& path, const std::string& title, const Params& p, const Traj& a, const Traj* b) {
  std::ofstream(path) << traj_svg_str(title, p, a, b);
}

// ------------------------------------------------------------------ RNG and helpers
struct Rng {
  uint64_t s[4];
  explicit Rng(uint64_t seed) {
    for (auto& x : s) { seed += 0x9e3779b97f4a7c15ULL; uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL; z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL; x = z ^ (z >> 31); }
  }
  uint64_t next() {
    auto rotl = [](uint64_t x, int k) { return (x << k) | (x >> (64 - k)); };
    const uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
    return r;
  }
  double u01() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

template <class F> static void parallel_for(size_t n, int threads, F fn) {
  std::atomic<size_t> next{0};
  std::vector<std::thread> pool;  // std::thread (not jthread): works on every NDK toolchain
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { for (;;) { const size_t i = next.fetch_add(1); if (i >= n) break; fn(i); } });
  for (auto& th : pool) th.join();
}

// ------------------------------------------------------------------ screening
struct Dim { std::string name; double lo, hi; bool lg; size_t off; };
struct ScreenCfg {
  std::vector<Dim> dims;
  int samples = 2000, refine_top = 3, nm_iters = 80, threads = 0, top_k = 10;
  double seed = 12345, wfactor = 2, w_ext = 5;
  std::string prefix = "screen";
};

static void load_screen_stream(std::istream& in, ScreenCfg& c, Params& base, std::vector<std::string>& err) {
  auto reg = registry(base);
  std::string line; int ln = 0;
  while (std::getline(in, line)) {
    ++ln;
    if (auto k = line.find('#'); k != std::string::npos) line.erase(k);
    auto eq = line.find('=');
    if (eq == std::string::npos) { if (!trim(line).empty()) err.push_back(errs_line(ln, "expected 'name = value'")); continue; }
    const std::string key = trim(line.substr(0, eq));
    std::istringstream rest(line.substr(eq + 1));
    std::vector<std::string> tok; std::string w;
    while (rest >> w) tok.push_back(w);
    auto num = [&](const std::string& s, double& v) { char* e = nullptr; v = std::strtod(s.c_str(), &e); return !s.empty() && *e == '\0' && std::isfinite(v); };
    double v = 0;
    if (key == "prefix") { if (tok.size() == 1) c.prefix = tok[0]; else err.push_back(errs_line(ln, "prefix needs one word")); continue; }
    const std::map<std::string, double*> ctl = {{"seed", &c.seed}, {"weight_factor", &c.wfactor}, {"w_ext", &c.w_ext}};
    const std::map<std::string, int*> ictl = {{"samples", &c.samples}, {"refine_top", &c.refine_top}, {"nm_iters", &c.nm_iters},
                                              {"threads", &c.threads}, {"top_k", &c.top_k}};
    if (ctl.count(key)) { if (tok.size() == 1 && num(tok[0], v)) *ctl.at(key) = v; else err.push_back(errs_line(ln, "bad value for " + key)); continue; }
    if (ictl.count(key)) { if (tok.size() == 1 && num(tok[0], v)) *ictl.at(key) = (int)v; else err.push_back(errs_line(ln, "bad value for " + key)); continue; }
    const Entry* hit = nullptr;
    for (auto& e : reg) if (key == e.name) hit = &e;
    if (!hit) { err.push_back(errs_line(ln, "unknown parameter '" + key + "'")); continue; }
    double lo, hi;
    if (tok.size() < 2 || !num(tok[0], lo) || !num(tok[1], hi) || lo >= hi) { err.push_back(errs_line(ln, "need 'min max [log]' with min < max for " + key)); continue; }
    const bool lg = tok.size() > 2 && tok[2] == "log";
    if (lg && lo <= 0) { err.push_back(errs_line(ln, "log range needs min > 0 for " + key)); continue; }
    c.dims.push_back({key, lo, hi, lg, (size_t)((char*)hit->ptr - (char*)&base)});
  }
  if (c.dims.empty()) err.push_back("ERROR: screen file defines no search dimensions");
  if (c.samples < 10) err.push_back("ERROR: samples must be >= 10");
}

[[maybe_unused]] static void load_screen(const std::string& path, ScreenCfg& c, Params& base, std::vector<std::string>& err) {
  std::ifstream in(path);
  if (!in) { err.push_back("ERROR: cannot open " + path); return; }
  load_screen_stream(in, c, base, err);
}

struct Eval {
  std::vector<double> x;
  Result r;
  bool valid = false, feasible = false;
  double pen = 0, F = 0;
  int cls = 2;  // 0 = feasible + extinction, 1 = feasible, 2 = infeasible
};

static double dimval(const Dim& d, double u) {
  u = std::clamp(u, 0.0, 1.0);
  return d.lg ? std::exp(std::log(d.lo) + u * (std::log(d.hi) - std::log(d.lo))) : d.lo + u * (d.hi - d.lo);
}

static Eval evaluate(const Params& base, const ScreenCfg& c, const std::vector<double>& u) {
  Eval e;
  Params q = base;
  for (size_t j = 0; j < c.dims.size(); ++j) {
    const double v = dimval(c.dims[j], u[j]);
    *(double*)((char*)&q + c.dims[j].off) = v;
    e.x.push_back(v);
  }
  q.dt_out = std::max(q.dt_out, 1.0);  // coarser output grid in screening (peaks are tracked every step)
  std::vector<std::string> tmp;
  validate(q, tmp);
  for (auto& s : tmp) if (s.rfind("ERROR", 0) == 0) { e.F = 1e12; e.pen = 1e12; return e; }
  e.r = run(q, nullptr, tmp);
  if (!e.r.ok) { e.F = 1e12; e.pen = 1e12; return e; }
  e.valid = true;
  e.feasible = e.r.peakC <= q.C_crit && e.r.peakH <= q.H_crit;
  e.pen = 1e3 * std::max(0.0, e.r.peakC / q.C_crit - 1) + 1e3 * std::max(0.0, e.r.peakH / q.H_crit - 1)
        + c.w_ext * std::log10(std::max(e.r.Vfinal, q.v_ext) / q.v_ext);
  e.F = Jw(q, e.r) + e.pen;
  e.cls = !e.feasible ? 2 : (e.r.extinct ? 0 : 1);
  return e;
}

// Nelder-Mead in the unit cube
static Eval nelder_mead(const Params& base, const ScreenCfg& c, std::vector<double> u0, int& evals) {
  const size_t d = u0.size();
  auto f = [&](std::vector<double>& u) { for (auto& v : u) v = std::clamp(v, 0.0, 1.0); ++evals; return evaluate(base, c, u); };
  std::vector<std::vector<double>> V(d + 1, u0);
  std::vector<Eval> E(d + 1);
  for (size_t i = 0; i < d; ++i) { V[i + 1][i] += (V[i + 1][i] > 0.9 ? -0.1 : 0.1); }
  for (size_t i = 0; i <= d; ++i) E[i] = f(V[i]);
  for (int it = 0; it < c.nm_iters; ++it) {
    std::vector<size_t> o(d + 1); std::iota(o.begin(), o.end(), 0);
    std::sort(o.begin(), o.end(), [&](size_t a, size_t b) { return E[a].F < E[b].F; });
    auto V2 = V; auto E2 = E;
    for (size_t i = 0; i <= d; ++i) { V[i] = V2[o[i]]; E[i] = E2[o[i]]; }
    if (std::fabs(E[d].F - E[0].F) < 1e-7 * (std::fabs(E[0].F) + 1e-9)) break;
    std::vector<double> cen(d, 0.0);
    for (size_t i = 0; i < d; ++i) for (size_t j = 0; j < d; ++j) cen[j] += V[i][j] / d;
    auto mix = [&](double t) { std::vector<double> v(d); for (size_t j = 0; j < d; ++j) v[j] = cen[j] + t * (V[d][j] - cen[j]); return v; };
    auto xr = mix(-1.0); Eval er = f(xr);
    if (er.F < E[0].F) { auto xe = mix(-2.0); Eval ee = f(xe); if (ee.F < er.F) { V[d] = xe; E[d] = ee; } else { V[d] = xr; E[d] = er; } }
    else if (er.F < E[d - 1].F) { V[d] = xr; E[d] = er; }
    else {
      auto xc = er.F < E[d].F ? mix(-0.5) : mix(0.5); Eval ec = f(xc);
      if (ec.F < std::min(er.F, E[d].F)) { V[d] = xc; E[d] = ec; }
      else for (size_t i = 1; i <= d; ++i) { for (size_t j = 0; j < d; ++j) V[i][j] = V[0][j] + 0.5 * (V[i][j] - V[0][j]); E[i] = f(V[i]); }
    }
  }
  size_t b = 0;
  for (size_t i = 1; i <= d; ++i) if (E[i].F < E[b].F) b = i;
  return E[b];
}

static std::vector<double> ranks(const std::vector<double>& v) {
  std::vector<size_t> o(v.size()); std::iota(o.begin(), o.end(), 0);
  std::sort(o.begin(), o.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
  std::vector<double> r(v.size());
  for (size_t i = 0; i < o.size();) {
    size_t j = i; while (j + 1 < o.size() && v[o[j + 1]] == v[o[i]]) ++j;
    for (size_t k = i; k <= j; ++k) r[o[k]] = 0.5 * (i + j) + 1;
    i = j + 1;
  }
  return r;
}
static bool invert(std::vector<std::vector<double>>& A) {
  const size_t n = A.size();
  std::vector<std::vector<double>> I(n, std::vector<double>(n, 0.0));
  for (size_t i = 0; i < n; ++i) I[i][i] = 1;
  for (size_t c = 0; c < n; ++c) {
    size_t p = c; for (size_t r = c + 1; r < n; ++r) if (std::fabs(A[r][c]) > std::fabs(A[p][c])) p = r;
    if (std::fabs(A[p][c]) < 1e-12) return false;
    std::swap(A[p], A[c]); std::swap(I[p], I[c]);
    const double d = A[c][c];
    for (size_t j = 0; j < n; ++j) { A[c][j] /= d; I[c][j] /= d; }
    for (size_t r = 0; r < n; ++r) if (r != c) { const double f = A[r][c];
      for (size_t j = 0; j < n; ++j) { A[r][j] -= f * A[c][j]; I[r][j] -= f * I[c][j]; } }
  }
  A = I; return true;
}
// partial rank correlation of every input with output y (Marino et al. 2008)
static std::vector<double> prcc(const std::vector<std::vector<double>>& X, const std::vector<double>& y) {
  const size_t d = X.size(), n = y.size();
  std::vector<std::vector<double>> R;
  for (auto& col : X) R.push_back(ranks(col));
  R.push_back(ranks(y));
  std::vector<std::vector<double>> C(d + 1, std::vector<double>(d + 1, 0.0));
  std::vector<double> mean(d + 1, 0.0), sd(d + 1, 0.0);
  for (size_t i = 0; i <= d; ++i) { for (double v : R[i]) mean[i] += v / n; for (double v : R[i]) sd[i] += (v - mean[i]) * (v - mean[i]); sd[i] = std::sqrt(sd[i]); }
  for (size_t i = 0; i <= d; ++i) for (size_t j = 0; j <= d; ++j) {
    double s = 0; for (size_t k = 0; k < n; ++k) s += (R[i][k] - mean[i]) * (R[j][k] - mean[j]);
    C[i][j] = (sd[i] > 0 && sd[j] > 0) ? s / (sd[i] * sd[j]) : (i == j ? 1.0 : 0.0);
  }
  std::vector<double> out(d, kNaN);
  if (!invert(C)) return out;
  for (size_t j = 0; j < d; ++j) out[j] = -C[j][d] / std::sqrt(C[j][j] * C[d][d]);
  return out;
}

static std::string fv(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }

static int screen(Params base, const ScreenCfg& c, std::vector<std::string>& err) {
  const auto t0 = std::chrono::steady_clock::now();
  const int nt = c.threads > 0 ? c.threads : std::max(1u, std::thread::hardware_concurrency());
  const size_t n = (size_t)c.samples, d = c.dims.size();
  // Latin hypercube
  Rng rng((uint64_t)c.seed);
  std::vector<std::vector<double>> U(n, std::vector<double>(d));
  for (size_t j = 0; j < d; ++j) {
    std::vector<size_t> perm(n); std::iota(perm.begin(), perm.end(), 0);
    for (size_t i = n - 1; i > 0; --i) std::swap(perm[i], perm[rng.next() % (i + 1)]);
    for (size_t i = 0; i < n; ++i) U[i][j] = (perm[i] + rng.u01()) / n;
  }
  std::vector<Eval> E(n);
  parallel_for(n, nt, [&](size_t i) { E[i] = evaluate(base, c, U[i]); });

  // untreated reference
  Params ub = base; ub.tip_dose = 0; std::vector<std::string> tmp;
  Traj trU; Result ru = run(ub, &trU, tmp);

  // ranking (class, then scalar F)
  std::vector<size_t> ord; for (size_t i = 0; i < n; ++i) if (E[i].valid) ord.push_back(i);
  auto better = [&](size_t a, size_t b) { if (E[a].cls != E[b].cls) return E[a].cls < E[b].cls; return E[a].F < E[b].F; };
  std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return better(a, b) || (!better(b, a) && a < b); });
  size_t nValid = ord.size(), nFeas = 0, nExt = 0;
  for (size_t i : ord) { nFeas += E[i].feasible; nExt += E[i].cls == 0; }
  if (nValid == 0) { err.push_back("ERROR: no valid samples - check the search bounds"); return 1; }

  // Pareto front over (peak V_W, peak C, exposure U) among feasible samples
  std::vector<size_t> feas; for (size_t i : ord) if (E[i].feasible) feas.push_back(i);
  std::sort(feas.begin(), feas.end(), [&](size_t a, size_t b) { return E[a].r.peakV < E[b].r.peakV || (E[a].r.peakV == E[b].r.peakV && a < b); });
  std::vector<size_t> front;
  for (size_t i : feas) {
    bool dom = false;
    for (size_t f : front) if (E[f].r.peakC <= E[i].r.peakC && E[f].r.I[3] <= E[i].r.I[3] &&
        (E[f].r.peakC < E[i].r.peakC || E[f].r.I[3] < E[i].r.I[3] || E[f].r.peakV < E[i].r.peakV)) { dom = true; break; }
    if (!dom) front.push_back(i);
  }
  std::vector<char> onFront(n, 0); for (size_t f : front) onFront[f] = 1;

  // Nelder-Mead refinement of the best few
  struct Ref { size_t from; Eval e; int evals; };
  std::vector<Ref> refined;
  for (int k = 0; k < c.refine_top && k < (int)ord.size(); ++k) {
    int ev = 0; Eval best = nelder_mead(base, c, U[ord[k]], ev);
    refined.push_back({ord[k], best, ev});
  }
  std::sort(refined.begin(), refined.end(), [](const Ref& a, const Ref& b) { return a.e.F < b.e.F; });

  // weight sensitivity: re-rank under scaled objective weights
  const double w0[4] = {base.w1, base.w2, base.w3, base.w4};
  auto score = [&](size_t i, const double* w) { return w[0] * E[i].r.I[0] + w[1] * E[i].r.I[1] + w[2] * E[i].r.I[2] + w[3] * E[i].r.I[3] + E[i].pen; };
  const size_t win = ord[0];
  const char* wn[4] = {"w1 (virus)", "w2 (cytokine)", "w3 (pulse)", "w4 (exposure)"};
  std::ostringstream ws; int same = 0, tot = 0, worst = 1;
  for (int k = 0; k < 4; ++k) for (int s = 0; s < 2; ++s) {
    double w[4] = {w0[0], w0[1], w0[2], w0[3]}; w[k] *= (s ? 1.0 / c.wfactor : c.wfactor);
    size_t top = ord[0]; int rankWin = 1;
    for (size_t i : ord) { auto lt = [&](size_t a, size_t b) { return E[a].cls != E[b].cls ? E[a].cls < E[b].cls : score(a, w) < score(b, w); };
      if (lt(i, top)) top = i;
      if (i != win && lt(i, win)) ++rankWin; }
    ++tot; same += (top == win); worst = std::max(worst, rankWin);
    ws << "  " << wn[k] << (s ? " / " : " x ") << fv(c.wfactor) << " : top-1 " << (top == win ? "unchanged" : "changed (sample " + std::to_string(top) + ")")
       << ", baseline winner rank " << rankWin << "\n";
  }

  // PRCC
  std::vector<std::vector<double>> X(d); std::vector<double> yV, yC, yU;
  for (size_t i : ord) { for (size_t j = 0; j < d; ++j) X[j].push_back(E[i].x[j]);
    yV.push_back(std::log10(std::max(E[i].r.peakV, 1e-4))); yC.push_back(E[i].r.peakC); yU.push_back(E[i].r.I[3]); }
  auto pV = prcc(X, yV), pC = prcc(X, yC), pU = prcc(X, yU);

  // ---- report
  const std::string rep = c.prefix + "_report.txt";
  std::ofstream o(rep);
  o << "# TIP-profile screening report\n";
  o << "samples = " << n << ", valid = " << nValid << ", feasible (C<=C_crit, H<=H_crit) = " << nFeas
    << ", feasible + WT extinction = " << nExt << ", Pareto front size = " << front.size() << "\n";
  o << "seed = " << (uint64_t)c.seed << ", t_end = " << base.t_end << " d, V_ext = " << base.v_ext << "\n";
  o << "R0_WT = " << fv(R0(base)) << "\n";
  o << "untreated reference: peak_V_W = " << fv(ru.peakV) << ", peak_C = " << fv(ru.peakC) << ", peak_H = " << fv(ru.peakH)
    << ", extinct = " << (ru.extinct ? "yes" : "no") << ", J = " << fv(Jw(base, ru)) << "\n\n";
  o << "class: A = feasible + extinction (high priority), B = feasible, C = violates safety bound\n";
  auto head = [&] { o << "rank class"; for (auto& dm : c.dims) o << " " << dm.name; o << " peakV peakC peakH t_ext U_AUC_VT F\n"; };
  auto rowf = [&](size_t id, const Eval& e) {
    o << "(" << id << ") " << "ABC"[e.cls];
    for (double v : e.x) o << " " << fv(v);
    o << " " << fv(e.r.peakV) << " " << fv(e.r.peakC) << " " << fv(e.r.peakH) << " " << (e.r.extinct ? fv(e.r.tExt) : "none")
      << " " << fv(e.r.I[3]) << " " << fv(e.F) << "\n";
  };
  o << "== Top " << c.top_k << " profiles (sampled) ==\n"; head();
  for (int k = 0; k < c.top_k && k < (int)ord.size(); ++k) { o << (k + 1) << " "; rowf(ord[k], E[ord[k]]); }
  o << "\n== Nelder-Mead refinement of the top " << refined.size() << " ==\n"; head();
  for (auto& r : refined) { o << "from " << r.from << " (" << r.evals << " evals) "; rowf(r.from, r.e); o << "  F before = " << fv(E[r.from].F) << "\n"; }
  o << "\n== Pareto front (peak V_W, peak C, exposure), sorted by peak V_W; first 25 of " << front.size() << " ==\n"; head();
  for (size_t k = 0; k < front.size() && k < 25; ++k) { o << (k + 1) << " "; rowf(front[k], E[front[k]]); }
  o << "\n== PRCC (|value| near 1 = strong influence on the output) ==\nparameter  log10_peakV  peakC  exposure_U\n";
  for (size_t j = 0; j < d; ++j) o << c.dims[j].name << "  " << fv(pV[j]) << "  " << fv(pC[j]) << "  " << fv(pU[j]) << "\n";
  o << "\n== Weight sensitivity (each weight scaled by x" << fv(c.wfactor) << " and 1/" << fv(c.wfactor) << ") ==\n" << ws.str();
  o << "  baseline winner stays top-1 in " << same << " of " << tot << " scenarios (worst rank " << worst << ")\n";
  o << "\nNote: ranking is conditional on the illustrative parameter set; all outputs are simulated, not predictions.\n";
  o.close();

  std::ofstream cs(c.prefix + "_samples.csv");
  cs << "id"; for (auto& dm : c.dims) cs << "," << dm.name; cs << ",peakV,peakC,peakH,extinct,t_ext,U,F,class,pareto\n";
  for (size_t i = 0; i < n; ++i) {
    cs << i; for (size_t j = 0; j < d; ++j) cs << "," << E[i].x[j];
    cs << "," << E[i].r.peakV << "," << E[i].r.peakC << "," << E[i].r.peakH << "," << E[i].r.extinct << "," << E[i].r.tExt
       << "," << E[i].r.I[3] << "," << E[i].F << "," << "ABC"[E[i].cls] << "," << (int)onFront[i] << "\n";
  }

  // Pareto scatter
  { std::ostringstream s;
    const double W = 700, H = 460, L = 70, R = 20, T = 40, B = 48;
    double xl = 1e300, xh = -1e300, yh = base.C_crit * 1.05;
    auto lx = [&](double v) { return std::log10(std::max(v, 1e-4)); };
    for (size_t i : ord) { xl = std::min(xl, lx(E[i].r.peakV)); xh = std::max(xh, lx(E[i].r.peakV)); yh = std::max(yh, E[i].r.peakC * 1.02); }
    xl = std::min(xl, lx(ru.peakV)); xh = std::max(xh, lx(ru.peakV)); yh = std::max(yh, ru.peakC * 1.02);
    xl = std::floor(xl); xh = std::ceil(xh); if (xh <= xl) xh = xl + 1;
    yh = std::min(yh, base.C_crit * 2.0);
    auto px = [&](double v) { return L + (lx(v) - xl) / (xh - xl) * (W - L - R); };
    auto py = [&](double v) { return T + (1 - std::min(v, yh) / yh) * (H - T - B); };
    s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H + 30 << "' viewBox='0 0 " << W << " " << H + 30 << "' font-family='Arial,Helvetica,sans-serif'>\n<rect width='100%' height='100%' fill='white'/>\n";
    s << "<text x='" << L << "' y='22' font-size='15' font-weight='bold'>TIP screening: peak WT viral load vs peak cytokine</text>\n";
    s << "<rect x='" << L << "' y='" << T << "' width='" << W - L - R << "' height='" << H - T - B << "' fill='none' stroke='#888'/>\n";
    for (int k = 0; k <= 4; ++k) { const double fx = xl + (xh - xl) * k / 4, fy = yh * k / 4;
      s << "<text x='" << L + (W - L - R) * k / 4 << "' y='" << H - B + 16 << "' font-size='11' text-anchor='middle'>1e" << fmt(fx) << "</text>"
        << "<text x='" << L - 7 << "' y='" << T + (1 - fy / yh) * (H - T - B) + 4 << "' font-size='11' text-anchor='end'>" << fmt(fy) << "</text>\n"; }
    s << "<text x='" << L + (W - L - R) / 2 << "' y='" << H - 12 << "' font-size='12' text-anchor='middle'>peak V_W (copies/mL, log scale)</text>\n";
    s << "<text x='16' y='" << T + (H - T - B) / 2 << "' font-size='12' text-anchor='middle' transform='rotate(-90 16 " << T + (H - T - B) / 2 << ")'>peak cytokine C (pg/mL)</text>\n";
    s << "<line x1='" << L << "' y1='" << py(base.C_crit) << "' x2='" << W - R << "' y2='" << py(base.C_crit) << "' stroke='#A32D2D' stroke-dasharray='2 3'/><text x='" << W - R - 3 << "' y='" << py(base.C_crit) - 4 << "' font-size='11' text-anchor='end' fill='#A32D2D'>C_crit</text>\n";
    for (size_t i : ord) { if (E[i].cls == 2 || onFront[i]) continue;
      s << "<circle cx='" << px(E[i].r.peakV) << "' cy='" << py(E[i].r.peakC) << "' r='2.5' fill='" << (E[i].cls == 0 ? "#1D9E75" : "#85B7EB") << "' fill-opacity='0.6'/>\n"; }
    for (size_t i : ord) if (E[i].cls == 2) s << "<circle cx='" << px(E[i].r.peakV) << "' cy='" << py(E[i].r.peakC) << "' r='2' fill='#B4B2A9' fill-opacity='0.5'/>\n";
    for (size_t f : front) s << "<circle cx='" << px(E[f].r.peakV) << "' cy='" << py(E[f].r.peakC) << "' r='4' fill='#0C447C' stroke='white'/>\n";
    s << "<path d='M" << px(ru.peakV) - 6 << " " << py(ru.peakC) - 6 << "L" << px(ru.peakV) + 6 << " " << py(ru.peakC) + 6 << "M" << px(ru.peakV) - 6 << " " << py(ru.peakC) + 6 << "L" << px(ru.peakV) + 6 << " " << py(ru.peakC) - 6 << "' stroke='#A32D2D' stroke-width='3'/>\n";
    const char* lg[5][2] = {{"#0C447C", "Pareto front"}, {"#1D9E75", "feasible + extinction"}, {"#85B7EB", "feasible"}, {"#B4B2A9", "violates a safety bound"}, {"#A32D2D", "untreated (x)"}};
    double lxp = L;
    for (auto& it : lg) { s << "<circle cx='" << lxp + 5 << "' cy='" << H + 14 << "' r='4' fill='" << it[0] << "'/><text x='" << lxp + 14 << "' y='" << H + 18 << "' font-size='11'>" << it[1] << "</text>\n"; lxp += 30 + 6.3 * std::string(it[1]).size(); }
    s << "</svg>\n"; std::ofstream(c.prefix + "_pareto.svg") << s.str(); }

  // best profile trajectory vs untreated
  { const Eval& bst = refined.empty() || refined[0].e.F >= E[win].F ? E[win] : refined[0].e;
    Params q = base;
    for (size_t j = 0; j < d; ++j) *(double*)((char*)&q + c.dims[j].off) = bst.x[j];
    validate(q, tmp);
    Traj tb; run(q, &tb, tmp);
    write_traj_svg(c.prefix + "_best.svg", "Best screened TIP profile vs untreated", q, tb, &trU); }

  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("screened %zu profiles on %d thread(s) in %.1f s: valid %zu, feasible %zu, feasible+extinct %zu, Pareto %zu\n",
              n, nt, secs, nValid, nFeas, nExt, front.size());
  std::printf("best sampled F = %.4g  (class %c)  | refined best F = %.4g\n", E[win].F, "ABC"[E[win].cls], refined.empty() ? E[win].F : refined[0].e.F);
  std::printf("wrote %s, %s_samples.csv, %s_pareto.svg, %s_best.svg\n", rep.c_str(), c.prefix.c_str(), c.prefix.c_str(), c.prefix.c_str());
  return 0;
}

// ------------------------------------------------------------------ self-test
[[maybe_unused]] static State fixed_run(const Params& p, int mult) {
  State y{}; y[iT] = p.Ts; y[iVW] = p.VW0; y[iC] = p.Cs; y[iH] = p.Hs;
  const double seg1 = p.tip_time, seg2 = p.t_end - p.tip_time;
  if (seg1 > 0) { const int n = (int)std::ceil(seg1 / p.h) * mult; for (int i = 0; i < n; ++i) rk4(p, y, seg1 / n); }
  y[iVT] += p.tip_dose;
  if (seg2 > 0) { const int n = (int)std::ceil(seg2 / p.h) * mult; for (int i = 0; i < n; ++i) rk4(p, y, seg2 / n); }
  return y;
}
[[maybe_unused]] static double dist(const State& a, const State& b) {
  double m = 0;
  for (int i = 0; i < kDyn; ++i) m = std::max(m, std::fabs(a[i] - b[i]) / std::max(1.0, std::fabs(b[i])));
  return m;
}


// ====================================================================== C API (for Kotlin / JNI)
// All text in, text out: no files needed for single runs, so it works inside an Android sandbox.
// Params text = the same "name = value" lines as input.txt (any subset; missing names use defaults).
// Every returned string must be released with ebov_free().
static char* dup_str(const std::string& s) {
  char* p = (char*)std::malloc(s.size() + 1);
  if (p) std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}
static bool has_err(const std::vector<std::string>& e) { for (auto& s : e) if (s.rfind("ERROR", 0) == 0) return true; return false; }
static std::string join(const std::vector<std::string>& e) { std::string o; for (auto& s : e) o += s + "\n"; return o; }

extern "C" {
const char* ebov_version() { return "ebov-tip-engine 1.0 (C++20)"; }
void ebov_free(char* p) { std::free(p); }

// format: 0 = summary (key=value lines + ERROR/WARNING lines), 1 = trajectory table, 2 = SVG plot
char* ebov_run(const char* params_text, int format) {
  Params p; std::vector<std::string> err;
  std::istringstream in(params_text ? params_text : "");
  load_stream(in, p, err);
  if (!has_err(err)) validate(p, err);
  if (has_err(err)) return dup_str(join(err));
  Traj tr; Result r = run(p, &tr, err);
  const bool haveU = p.tip_dose > 0;
  Traj trU; Result ru;
  if (haveU) { Params ub = p; ub.tip_dose = 0; std::vector<std::string> tmp; ru = run(ub, &trU, tmp); }
  if (format == 2) return dup_str(traj_svg_str(haveU ? "EBOV-TIP trajectory (with TIP vs untreated)" : "EBOV within-host trajectory (untreated)", p, tr, haveU ? &trU : nullptr));
  if (format == 1) {
    std::ostringstream o; o << "time T E_W I_W V_W E_Co I_Co V_T C H J P\n";
    for (size_t k = 0; k < tr.t.size(); ++k) { const State& y = tr.y[k];
      o << tr.t[k]; for (int i = 0; i < 9; ++i) o << ' ' << y[i];
      o << ' ' << p.w1 * y[iJV] + p.w2 * y[iJC] + p.w3 * y[iJH] + p.w4 * y[iU] << ' ' << y[iP] << "\n"; }
    return dup_str(o.str());
  }
  std::ostringstream o;
  o << "R0_WT=" << R0(p) << "\npeak_V_W=" << r.peakV << "\nt_peak_V_W=" << r.tPeakV << "\npeak_C=" << r.peakC
    << "\npeak_H=" << r.peakH << "\nC_within_bound=" << (r.peakC <= p.C_crit) << "\nH_within_bound=" << (r.peakH <= p.H_crit)
    << "\nextinct=" << r.extinct << "\nt_extinct=" << (r.extinct ? r.tExt : -1.0) << "\nexposure_AUC_V_T=" << r.I[3]
    << "\nobjective_J=" << Jw(p, r) << "\n";
  if (haveU) o << "untreated_peak_V_W=" << ru.peakV << "\nuntreated_peak_C=" << ru.peakC << "\nuntreated_peak_H=" << ru.peakH << "\n";
  if (R0(p) <= 1) err.push_back("WARNING: R_0 <= 1 for WT alone; not an acute-infection regime");
  o << join(err);
  return dup_str(o.str());
}

// Screening: writes <out_prefix>_report.txt, _samples.csv, _pareto.svg, _best.svg and returns the report text
// (or ERROR lines). out_prefix example on Android: "/data/data/<pkg>/files/screen".
char* ebov_screen(const char* params_text, const char* screen_text, const char* out_prefix) {
  Params p; std::vector<std::string> err;
  std::istringstream in(params_text ? params_text : "");
  load_stream(in, p, err);
  if (!has_err(err)) validate(p, err);
  ScreenCfg c;
  std::istringstream sin(screen_text ? screen_text : "");
  load_screen_stream(sin, c, p, err);
  if (has_err(err)) return dup_str(join(err));
  if (out_prefix && *out_prefix) c.prefix = out_prefix;
  if (screen(p, c, err) != 0) return dup_str(join(err));
  std::ifstream rep(c.prefix + "_report.txt");
  std::ostringstream o; o << rep.rdbuf();
  return dup_str(join(err) + o.str());
}
}  // extern "C"

#ifndef EBOV_NO_MAIN
int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  const bool self = mode == "--selftest", scr = mode == "--screen";
  const int a0 = (self || scr) ? 2 : 1;
  auto arg = [&](int k, const char* def) { return argc > a0 + k ? std::string(argv[a0 + k]) : std::string(def); };
  const std::string scfg = scr ? arg(0, "screen.txt") : "";
  const std::string fin = scr ? arg(1, "input.txt") : arg(0, "input.txt");
  const std::string fout = (self || scr) ? "" : arg(1, "output.txt");
  const std::string ferr = scr ? arg(2, "errors.txt") : (self ? "errors.txt" : arg(2, "errors.txt"));

  Params p;
  std::vector<std::string> err;
  load(fin, p, err);
  if (err.empty()) validate(p, err);
  auto has_error = [&] { for (auto& s : err) if (s.rfind("ERROR", 0) == 0) return true; return false; };
  auto flush_err = [&] { std::ofstream e(ferr); for (auto& s : err) e << s << "\n"; };
  if (has_error()) { flush_err(); std::fprintf(stderr, "Input rejected, see %s\n", ferr.c_str()); return 1; }

  if (self) {
    State a = fixed_run(p, 1), b = fixed_run(p, 2), c = fixed_run(p, 4);
    const double e1 = dist(a, b), e2 = dist(b, c), order = std::log2(e1 / e2);
    std::printf("RK4 order estimate: %.3f  (e1=%.3e, e2=%.3e)  %s\n", order, e1, e2, (order >= 3.8 && order <= 4.2) ? "PASS" : "CHECK");
    flush_err(); return 0;
  }
  if (scr) {
    ScreenCfg c;
    load_screen(scfg, c, p, err);
    if (has_error()) { flush_err(); std::fprintf(stderr, "Screen file rejected, see %s\n", ferr.c_str()); return 1; }
    const int rc = screen(p, c, err);
    flush_err(); return rc;
  }

  Traj tr; Result r = run(p, &tr, err);
  std::ofstream out(fout);
  out << "# time T E_W I_W V_W E_Co I_Co V_T C H J\n";
  for (size_t k = 0; k < tr.t.size(); ++k) {
    const State& y = tr.y[k];
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.6g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", tr.t[k], y[0], y[1], y[2], y[3], y[4], y[5], y[6], y[7], y[8],
                  p.w1 * y[iJV] + p.w2 * y[iJC] + p.w3 * y[iJH] + p.w4 * y[iU]);
    out << buf;
  }
  const double r0 = R0(p);
  if (r0 <= 1) err.push_back("WARNING: R_0 <= 1 for WT alone; not an acute-infection regime");
  if (r.reduced > 0) err.push_back("WARNING: step reduced for stability in " + std::to_string(r.reduced) + " output intervals");
  Traj trU; Result ru; const bool haveU = p.tip_dose > 0;
  if (haveU) { Params ub = p; ub.tip_dose = 0; std::vector<std::string> tmp; ru = run(ub, &trU, tmp); }
  out << "# ---- summary ----\n# R0_WT = " << r0 << "\n# R_eff_(entry+clearance only) = " << (1 - p.eps_ab) * r0 / (1 + p.kappa * p.eps_clr) << "\n";
  out << "# peak_V_W = " << r.peakV << " at t = " << r.tPeakV << " d" << (haveU ? "   (untreated: " + fv(ru.peakV) + ")" : "") << "\n";
  out << "# peak_C = " << r.peakC << (r.peakC > p.C_crit ? "  EXCEEDS C_crit" : "  within C_crit") << (haveU ? "   (untreated: " + fv(ru.peakC) + ")" : "") << "\n";
  out << "# peak_H = " << r.peakH << (r.peakH > p.H_crit ? "  EXCEEDS H_crit" : "  within H_crit") << (haveU ? "   (untreated: " + fv(ru.peakH) + ")" : "") << "\n";
  out << "# extinction (V_W < V_ext) = " << (r.extinct ? "YES" : "NO"); if (r.extinct) out << " , time = " << r.tExt << " d";
  out << "\n# objective_J = " << Jw(p, r) << "\n# exposure_AUC_V_T = " << r.I[3] << "\n";
  out.close();
  std::string svg = fout; if (auto dot = svg.rfind('.'); dot != std::string::npos) svg.erase(dot); svg += ".svg";
  write_traj_svg(svg, haveU ? "EBOV-TIP trajectory (with TIP vs untreated)" : "EBOV within-host trajectory (untreated)", p, tr, haveU ? &trU : nullptr);
  std::printf("R0=%.3g  peakV=%.3g (t=%.2f)  peakC=%.3g  peakH=%.3g  extinct=%s  J=%.4g  -> %s, %s\n",
              r0, r.peakV, r.tPeakV, r.peakC, r.peakH, r.extinct ? "yes" : "no", Jw(p, r), fout.c_str(), svg.c_str());
  flush_err();
  return r.ok ? 0 : 2;
}
#endif  // EBOV_NO_MAIN
