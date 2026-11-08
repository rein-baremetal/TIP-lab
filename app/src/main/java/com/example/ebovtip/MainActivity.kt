package com.example.ebovtip

import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.animation.DecelerateInterpolator
import android.webkit.WebView
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.util.Locale
import kotlin.math.abs
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.log10

class MainActivity : Activity() {

    private class P(
        val name: String, val label: String, val hint: String,
        val min: Double, val max: Double, val log: Boolean, val def: Double
    ) {
        fun valueAt(pos: Float): Double =
            if (log) exp(ln(min) + pos * (ln(max) - ln(min))) else min + pos * (max - min)
        fun posOf(v: Double): Float =
            (if (log) (ln(v) - ln(min)) / (ln(max) - ln(min)) else (v - min) / (max - min)).toFloat().coerceIn(0f, 1f)
    }

    private val params = listOf(
        P("tip_dose", "TIP dose", "copies/mL given at once", 1e5, 1e9, true, 1e9),
        P("tip_time", "Dosing day", "when the TIP is given", 0.0, 5.0, false, 1.0),
        P("c_T", "TIP clearance", "lower = longer-lived TIP", 0.1, 15.0, true, 0.2),
        P("alpha", "Hijack strength", "lower = stronger suppression of virus", 0.02, 0.4, false, 0.05),
        P("beta_T", "Cell entry", "how fast TIPs enter infected cells", 2.5e-7, 2e-6, true, 2e-6),
        P("p_T", "TIP production", "TIP output per co-infected cell", 500.0, 3000.0, false, 3000.0),
        P("eps_Ab", "Antibody block", "blocks entry of virus and TIP", 0.0, 0.9, false, 0.0),
        P("eps_Clr", "Enhanced clearance", "extra antibody clearance of virus", 0.0, 1.0, false, 0.0)
    )

    private val presets = listOf(
        "⚡ Strong TIP" to mapOf("tip_dose" to 1e9, "tip_time" to 1.0, "c_T" to 0.2, "alpha" to 0.05, "beta_T" to 2e-6, "p_T" to 3000.0, "eps_Ab" to 0.0, "eps_Clr" to 0.0),
        "🎯 Screened best" to mapOf("tip_dose" to 4e7, "tip_time" to 0.3, "c_T" to 0.18, "alpha" to 0.05, "beta_T" to 8e-7, "p_T" to 2900.0, "eps_Ab" to 0.0, "eps_Clr" to 0.0),
        "🧪 Fragile TIP" to mapOf("tip_dose" to 1e7, "tip_time" to 2.0, "c_T" to 10.0, "alpha" to 0.2, "beta_T" to 5e-7, "p_T" to 1500.0, "eps_Ab" to 0.0, "eps_Clr" to 0.0)
    )

    private val defaultScreenCfg = """
        beta_T   = 2.5e-7 2e-6 log
        alpha    = 0.05 0.4
        p_T      = 500 3000
        c_T      = 0.1 15 log
        tip_dose = 1e5 1e9 log
        tip_time = 0 5
        samples  = 1000
        seed     = 12345
        refine_top = 3
        top_k    = 10
    """.trimIndent()

    private lateinit var pal: Palette
    private val sliders = HashMap<String, IosSlider>()
    private lateinit var hero: SimView
    private lateinit var runBtn: TextView
    private lateinit var discoverBtn: TextView
    private lateinit var errorCard: TextView
    private lateinit var tilePeak: Tile
    private lateinit var tileCyt: Tile
    private lateinit var tileClear: Tile
    private lateinit var advanced: EditText
    private lateinit var screenCfg: EditText
    private lateinit var discovery: LinearLayout
    private lateinit var discCount: TextView
    private lateinit var discBest: TextView
    private lateinit var discReport: TextView
    private lateinit var web: WebView
    private lateinit var segBest: TextView
    private lateinit var segPareto: TextView
    private var bestSvg: String? = null
    private var paretoSvg: String? = null
    private var bestProfile: Map<String, Double> = emptyMap()
    private var pulse: ObjectAnimator? = null

    // ------------------------------------------------------------------ lifecycle
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        pal = Palette(this)
        window.setBackgroundDrawable(ColorDrawable(pal.bg))
        window.statusBarColor = pal.bg
        window.navigationBarColor = pal.bg
        if (!pal.dark && Build.VERSION.SDK_INT >= 23) window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
        val root = buildUi()
        setContentView(root)
        val content = root.getChildAt(0) as LinearLayout
        for (i in 0 until content.childCount) {
            val v = content.getChildAt(i)
            v.alpha = 0f
            v.translationY = dpf(34f)
            v.animate().alpha(1f).translationY(0f).setStartDelay(i * 70L).setDuration(520).setInterpolator(DecelerateInterpolator(2f)).start()
        }
    }

    // ------------------------------------------------------------------ small builders
    private fun tv(s: String, size: Float, color: Int, family: String = "sans-serif", bold: Boolean = false) = TextView(this).apply {
        text = s; textSize = size; setTextColor(color)
        typeface = Typeface.create(family, if (bold) Typeface.BOLD else Typeface.NORMAL)
    }

    private fun lp(w: Int, h: Int, weight: Float = 0f, top: Int = 0) =
        LinearLayout.LayoutParams(w, h, weight).apply { topMargin = dp(top) }

    private fun card() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = roundRect(pal.card, dpf(22f))
        setPadding(dp(16), dp(6), dp(16), dp(6))
    }

    private fun section(title: String) = tv(title.uppercase(), 12f, pal.sub, "sans-serif-medium").apply {
        letterSpacing = 0.08f; setPadding(dp(8), dp(10), 0, dp(2))
    }

    private fun pill(color1: Int, color2: Int) = GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, intArrayOf(color1, color2)).apply { cornerRadius = dpf(28f) }

    private fun fmt(v: Double): String = when {
        v == 0.0 -> "0"
        abs(v) >= 1000 || abs(v) < 0.01 -> String.format(Locale.US, "%.1e", v)
        abs(v) >= 100 -> String.format(Locale.US, "%.0f", v)
        abs(v) >= 10 -> String.format(Locale.US, "%.1f", v)
        else -> String.format(Locale.US, "%.2f", v)
    }

    private class Tile(ctx: Context, pal: Palette, title: String) {
        val view = LinearLayout(ctx)
        val value = TextView(ctx)
        val sub = TextView(ctx)
        init {
            view.orientation = LinearLayout.VERTICAL
            view.background = roundRect(pal.card, ctx.dpf(18f))
            view.setPadding(ctx.dp(12), ctx.dp(12), ctx.dp(12), ctx.dp(12))
            val t = TextView(ctx).apply {
                text = title.uppercase(); textSize = 10f; letterSpacing = 0.08f; setTextColor(pal.sub)
                typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            }
            value.apply { text = "—"; textSize = 21f; setTextColor(pal.text); typeface = Typeface.create("sans-serif-medium", Typeface.BOLD) }
            sub.apply { text = " "; textSize = 11f; setTextColor(pal.sub) }
            view.addView(t); view.addView(value); view.addView(sub)
        }
        fun countTo(target: Double, format: (Double) -> String, subText: String, subColor: Int) {
            sub.text = subText; sub.setTextColor(subColor)
            ValueAnimator.ofFloat(0f, 1f).apply {
                duration = 900; interpolator = DecelerateInterpolator(2f)
                addUpdateListener { value.text = format(target * it.animatedFraction) }
                start()
            }
        }
        fun setStatic(text: String, subText: String, subColor: Int) {
            value.text = text; sub.text = subText; sub.setTextColor(subColor)
            value.scaleX = 0.8f; value.scaleY = 0.8f
            value.animate().scaleX(1f).scaleY(1f).setDuration(350).start()
        }
    }

    // ------------------------------------------------------------------ UI
    private fun buildUi(): ScrollView {
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(20), dp(16), dp(40))
        }
        // large title
        val titleBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(4), 0, 0, dp(4)) }
        titleBox.addView(tv("TIP Lab", 34f, pal.text, "sans-serif-black"))
        titleBox.addView(tv("Ebola · Therapeutic Interfering Particles", 15f, pal.sub))
        col.addView(titleBox)

        // hero arena
        hero = SimView(this)
        hero.onFinished = { ok -> if (ok) hero.confirmHaptic() else hero.tick() }
        col.addView(hero, lp(ViewGroup.LayoutParams.MATCH_PARENT, dp(400), 0f, 14))

        // run button
        runBtn = tv("Run simulation", 17f, Color.WHITE, "sans-serif-medium", true).apply {
            gravity = Gravity.CENTER; background = pill(0xFF0A84FF.toInt(), 0xFF5E5CE6.toInt())
        }
        runBtn.pressable { runSim() }
        col.addView(runBtn, lp(ViewGroup.LayoutParams.MATCH_PARENT, dp(56), 0f, 14))

        errorCard = tv("", 13f, 0xFFFF453A.toInt(), "sans-serif-medium").apply {
            background = roundRect(if (pal.dark) 0xFF3A1B19.toInt() else 0xFFFFE5E3.toInt(), dpf(16f))
            setPadding(dp(14), dp(12), dp(14), dp(12)); visibility = View.GONE
        }
        col.addView(errorCard, lp(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0f, 10))

        // result tiles
        tilePeak = Tile(this, pal, "Peak virus"); tileCyt = Tile(this, pal, "Peak cytokine"); tileClear = Tile(this, pal, "Cleared")
        val tiles = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for ((i, t) in listOf(tilePeak, tileCyt, tileClear).withIndex()) {
            tiles.addView(t.view, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { if (i > 0) leftMargin = dp(8) })
        }
        col.addView(tiles, lp(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0f, 12))

        // presets
        col.addView(section("Presets"))
        val chips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(4), 0, dp(4)) }
        for ((name, vals) in presets) {
            val chip = tv(name, 14f, pal.blue, "sans-serif-medium").apply {
                background = roundRect(pal.card, dpf(20f)); setPadding(dp(16), dp(10), dp(16), dp(10))
            }
            chip.pressable { applyValues(vals) }
            chips.addView(chip, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { rightMargin = dp(8) })
        }
        col.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(chips) })

        // parameter sliders
        col.addView(section("TIP profile"))
        val pc = card()
        for ((idx, p) in params.withIndex()) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(10), 0, dp(2)) }
            val top = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            val names = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            names.addView(tv(p.label, 16f, pal.text, "sans-serif-medium"))
            names.addView(tv(p.hint, 12f, pal.sub))
            top.addView(names, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            val vl = tv("", 16f, pal.blue, "sans-serif-medium")
            top.addView(vl)
            row.addView(top)
            val s = IosSlider(this, pal)
            s.onChange = { pos -> vl.text = fmt(p.valueAt(pos)) }
            s.value = p.posOf(p.def)
            sliders[p.name] = s
            row.addView(s, lp(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            pc.addView(row)
            if (idx < params.size - 1) pc.addView(View(this).apply { setBackgroundColor(pal.sep) }, lp(ViewGroup.LayoutParams.MATCH_PARENT, 1))
        }
        col.addView(pc)

        // discovery
        col.addView(section("Discovery"))
        discoverBtn = tv("🔍  Discover the best TIP profile", 16f, pal.blue, "sans-serif-medium").apply {
            gravity = Gravity.CENTER; background = roundRect(pal.card, dpf(28f))
        }
        discoverBtn.pressable { doScreen() }
        col.addView(discoverBtn, lp(ViewGroup.LayoutParams.MATCH_PARENT, dp(54)))
        buildDiscovery(col)

        // advanced
        val advHeader = tv("Advanced  ›", 15f, pal.blue, "sans-serif-medium").apply { setPadding(dp(8), dp(14), 0, dp(6)) }
        col.addView(advHeader)
        val advBody = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE; background = roundRect(pal.card, dpf(22f)); setPadding(dp(14), dp(10), dp(14), dp(14)) }
        advBody.addView(tv("Any other engine parameter (name = value per line)", 12f, pal.sub))
        advanced = editor("ext_floor = 1", 3)
        advBody.addView(advanced)
        advBody.addView(tv("Screening search ranges (name = min max [log])", 12f, pal.sub).apply { setPadding(0, dp(12), 0, 0) })
        screenCfg = editor(defaultScreenCfg, 8)
        advBody.addView(screenCfg)
        col.addView(advBody)
        var open = false
        advHeader.setOnClickListener {
            open = !open
            advHeader.text = if (open) "Advanced  ⌄" else "Advanced  ›"
            toggle(advBody, open); it.tick()
        }

        col.addView(tv("Simulated results on illustrative parameters; not predictions and not for clinical use.", 11f, pal.sub).apply { setPadding(dp(8), dp(18), dp(8), 0) })
        return ScrollView(this).apply { addView(col); isVerticalScrollBarEnabled = false; overScrollMode = View.OVER_SCROLL_ALWAYS }
    }

    private fun editor(initial: String, lines: Int) = EditText(this).apply {
        setText(initial); minLines = lines; gravity = Gravity.TOP
        typeface = Typeface.MONOSPACE; textSize = 12.5f; setTextColor(pal.text)
        background = roundRect(pal.card2, dpf(12f)); setPadding(dp(10), dp(8), dp(10), dp(8))
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
    }

    private fun buildDiscovery(col: LinearLayout) {
        discovery = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; background = roundRect(pal.card, dpf(22f))
            setPadding(dp(16), dp(14), dp(16), dp(16)); visibility = View.GONE
        }
        discovery.addView(tv("PROFILES THAT CLEARED THE VIRUS", 11f, pal.sub, "sans-serif-medium").apply { letterSpacing = 0.08f })
        discCount = tv("", 40f, pal.green, "sans-serif-black")
        discovery.addView(discCount)
        discovery.addView(tv("BEST PROFILE FOUND", 11f, pal.sub, "sans-serif-medium").apply { letterSpacing = 0.08f; setPadding(0, dp(10), 0, 0) })
        discBest = tv("", 14f, pal.text, "monospace")
        discovery.addView(discBest)
        val apply = tv("Apply & run this profile", 15f, Color.WHITE, "sans-serif-medium", true).apply {
            gravity = Gravity.CENTER; background = pill(0xFF30D158.toInt(), 0xFF0A84FF.toInt())
        }
        apply.pressable { if (bestProfile.isNotEmpty()) { applyValues(bestProfile); runBtn.postDelayed({ runSim() }, 650) } }
        discovery.addView(apply, lp(ViewGroup.LayoutParams.MATCH_PARENT, dp(48), 0f, 12))

        val seg = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; background = roundRect(pal.card2, dpf(12f)); setPadding(dp(3), dp(3), dp(3), dp(3)) }
        segBest = tv("Best run", 13f, pal.text, "sans-serif-medium").apply { gravity = Gravity.CENTER; setPadding(0, dp(8), 0, dp(8)) }
        segPareto = tv("Trade-offs", 13f, pal.text, "sans-serif-medium").apply { gravity = Gravity.CENTER; setPadding(0, dp(8), 0, dp(8)) }
        seg.addView(segBest, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        seg.addView(segPareto, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        discovery.addView(seg, lp(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0f, 14))
        segBest.setOnClickListener { selectSeg(true); it.tick() }
        segPareto.setOnClickListener { selectSeg(false); it.tick() }

        web = WebView(this).apply { setBackgroundColor(Color.TRANSPARENT) }
        discovery.addView(web, lp(ViewGroup.LayoutParams.MATCH_PARENT, dp(300), 0f, 10))

        val rep = tv("Full report  ›", 14f, pal.blue, "sans-serif-medium").apply { setPadding(0, dp(12), 0, dp(4)) }
        discReport = tv("", 11f, pal.text, "monospace").apply { visibility = View.GONE; setTextIsSelectable(true) }
        var repOpen = false
        rep.setOnClickListener { repOpen = !repOpen; rep.text = if (repOpen) "Full report  ⌄" else "Full report  ›"; discReport.visibility = if (repOpen) View.VISIBLE else View.GONE; it.tick() }
        discovery.addView(rep); discovery.addView(discReport)
        col.addView(discovery, lp(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, 0f, 10))
    }

    private fun selectSeg(best: Boolean) {
        segBest.background = if (best) roundRect(pal.card, dpf(10f)) else null
        segPareto.background = if (!best) roundRect(pal.card, dpf(10f)) else null
        val svg = if (best) bestSvg else paretoSvg
        web.animate().alpha(0f).setDuration(120).withEndAction {
            if (svg != null) showSvg(svg)
            web.animate().alpha(1f).setDuration(220).start()
        }.start()
    }

    private fun showSvg(svg: String) {
        val bg = if (pal.dark) "#1C1C1E" else "#FFFFFF"
        val html = "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>" +
            "<style>body{margin:0;background:$bg}svg{width:100%;height:auto;border-radius:14px}</style></head><body>$svg</body></html>"
        web.loadDataWithBaseURL(null, html, "text/html", "utf-8", null)
    }

    private fun toggle(v: View, show: Boolean) {
        if (show) {
            v.measure(
                View.MeasureSpec.makeMeasureSpec(resources.displayMetrics.widthPixels - dp(32), View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED)
            )
            val target = v.measuredHeight
            v.layoutParams.height = 0; v.visibility = View.VISIBLE; v.requestLayout()
            ValueAnimator.ofInt(0, target).apply {
                duration = 320; interpolator = DecelerateInterpolator(2f)
                addUpdateListener { v.layoutParams.height = it.animatedValue as Int; v.requestLayout() }
                addListener(object : android.animation.AnimatorListenerAdapter() {
                    override fun onAnimationEnd(a: android.animation.Animator) { v.layoutParams.height = ViewGroup.LayoutParams.WRAP_CONTENT; v.requestLayout() }
                })
                start()
            }
        } else {
            val from = v.height
            ValueAnimator.ofInt(from, 0).apply {
                duration = 260; interpolator = DecelerateInterpolator(2f)
                addUpdateListener { v.layoutParams.height = it.animatedValue as Int; v.requestLayout() }
                addListener(object : android.animation.AnimatorListenerAdapter() {
                    override fun onAnimationEnd(a: android.animation.Animator) { v.visibility = View.GONE; v.layoutParams.height = ViewGroup.LayoutParams.WRAP_CONTENT }
                })
                start()
            }
        }
    }

    // ------------------------------------------------------------------ behaviour
    private fun applyValues(vals: Map<String, Double>) {
        for ((name, v) in vals) {
            val p = params.firstOrNull { it.name == name } ?: continue
            val s = sliders[name] ?: continue
            ValueAnimator.ofFloat(s.value, p.posOf(v)).apply {
                duration = 480; interpolator = DecelerateInterpolator(2f)
                addUpdateListener { s.value = it.animatedValue as Float }
                start()
            }
        }
    }

    private fun paramsText(): String {
        val sb = StringBuilder()
        for (p in params) {
            val pos = sliders[p.name]?.value ?: p.posOf(p.def)
            sb.append(p.name).append(" = ").append(String.format(Locale.US, "%.6g", p.valueAt(pos))).append('\n')
        }
        sb.append(advanced.text.toString()).append('\n')
        return sb.toString()
    }

    private fun setBusy(button: TextView, busy: Boolean, busyText: String, idleText: String) {
        pulse?.cancel()
        button.text = if (busy) busyText else idleText
        button.alpha = 1f
        if (busy) {
            pulse = ObjectAnimator.ofFloat(button, "alpha", 1f, 0.55f).apply { duration = 600; repeatCount = ValueAnimator.INFINITE; repeatMode = ValueAnimator.REVERSE; start() }
        }
    }

    private fun showError(msg: String) {
        errorCard.text = msg.trim()
        errorCard.visibility = View.VISIBLE
        ObjectAnimator.ofFloat(errorCard, "translationX", 0f, -dpf(14f), dpf(14f), -dpf(9f), dpf(9f), -dpf(4f), 0f).setDuration(420).start()
        errorCard.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
    }

    private fun parseKv(s: String): Map<String, String> =
        s.lines().filter { it.contains('=') && !it.startsWith("ERROR") && !it.startsWith("WARNING") }
            .associate { it.substringBefore('=').trim() to it.substringAfter('=').trim() }

    private fun parseTable(tab: String): List<FloatArray> {
        val rows = ArrayList<FloatArray>()
        for ((i, line) in tab.lines().withIndex()) {
            if (i == 0 || line.isBlank()) continue
            val t = line.trim().split(Regex("\\s+"))
            if (t.size < 10) continue
            rows.add(FloatArray(10) { k -> t[k].toFloatOrNull() ?: 0f })
        }
        return rows
    }

    private fun lg(v: Float): Float = log10(maxOf(v, 1e-4f))

    private fun runSim() {
        val p = paramsText()
        errorCard.visibility = View.GONE
        setBusy(runBtn, true, "Simulating…", "Run simulation")
        Thread {
            val sum = EngineBridge.runRaw(p, 0)
            if (!sum.contains("R0_WT=")) {
                runOnUiThread { setBusy(runBtn, false, "", "Run simulation"); showError(sum) }
                return@Thread
            }
            val tab = parseTable(EngineBridge.runRaw(p, 1))
            val ghost = parseTable(EngineBridge.runRaw(p + "\ntip_dose = 0\n", 1))
            runOnUiThread {
                setBusy(runBtn, false, "", "Run simulation")
                presentRun(parseKv(sum), tab, ghost, sum)
            }
        }.start()
    }

    private fun presentRun(kv: Map<String, String>, tab: List<FloatArray>, ghost: List<FloatArray>, raw: String) {
        if (tab.size < 2) { showError("No trajectory returned."); return }
        val n = minOf(tab.size, if (ghost.size >= 2) ghost.size else tab.size)
        val data = SimData(
            FloatArray(n) { tab[it][0] },
            FloatArray(n) { lg(tab[it][4]) },
            FloatArray(n) { lg(tab[it][7]) },
            FloatArray(n) { if (ghost.size >= 2) lg(ghost[it][4]) else -4f },
            FloatArray(n) { tab[it][8] },
            FloatArray(n) { tab[it][9] }
        )
        val extinct = kv["extinct"] == "1"
        val tExt = kv["t_extinct"]?.toDoubleOrNull() ?: -1.0
        val peak = kv["peak_V_W"]?.toDoubleOrNull() ?: 0.0
        val peakU = kv["untreated_peak_V_W"]?.toDoubleOrNull() ?: peak
        val peakC = kv["peak_C"]?.toDoubleOrNull() ?: 0.0
        val peakCU = kv["untreated_peak_C"]?.toDoubleOrNull() ?: peakC
        val safe = kv["C_within_bound"] == "1" && kv["H_within_bound"] == "1"
        val fast = extinct && tExt in 0.0..15.0
        val cut = if (peakU > 0) (1 - peak / peakU) * 100 else 0.0
        val sub = if (extinct) "Virus cleared on day " + String.format(Locale.US, "%.1f", tExt)
        else "Peak virus cut by " + String.format(Locale.US, "%.1f", cut) + "%"
        hero.show(data, extinct, sub, booleanArrayOf(extinct, safe, fast))

        tilePeak.countTo(peak, { fmt(it) }, if (peakU > peak) "−" + String.format(Locale.US, "%.1f", cut) + "% vs untreated" else "same as untreated", if (cut > 50) pal.green else pal.sub)
        tileCyt.countTo(peakC, { String.format(Locale.US, "%.0f", it) }, if (peakCU > 0) "untreated " + String.format(Locale.US, "%.0f", peakCU) + " pg/mL" else "pg/mL", pal.sub)
        if (extinct) tileClear.countTo(tExt, { "Day " + String.format(Locale.US, "%.1f", it) }, "virus eradicated", pal.green)
        else tileClear.setStatic("Not yet", "virus persists", pal.orange)
    }

    private fun doScreen() {
        val p = paramsText()
        val cfg = screenCfg.text.toString()
        val prefix = File(filesDir, "screen").absolutePath
        errorCard.visibility = View.GONE
        setBusy(discoverBtn, true, "Searching profiles…", "🔍  Discover the best TIP profile")
        Thread {
            val rep = EngineBridge.screenRaw(p, cfg, prefix)
            val best = File(prefix + "_best.svg").takeIf { it.exists() }?.readText()
            val pareto = File(prefix + "_pareto.svg").takeIf { it.exists() }?.readText()
            runOnUiThread {
                setBusy(discoverBtn, false, "", "🔍  Discover the best TIP profile")
                if (!rep.contains("TIP-profile screening report")) { showError(rep); return@runOnUiThread }
                presentDiscovery(rep, best, pareto)
            }
        }.start()
    }

    private fun presentDiscovery(rep: String, best: String?, pareto: String?) {
        bestSvg = best; paretoSvg = pareto
        val total = Regex("samples = (\\d+)").find(rep)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        val hits = Regex("WT extinction = (\\d+)").find(rep)?.groupValues?.get(1)?.toIntOrNull() ?: 0
        val lines = rep.lines()
        val topIdx = lines.indexOfFirst { it.startsWith("== Top") }
        val after = if (topIdx >= 0) lines.drop(topIdx + 1) else emptyList()
        val header = after.firstOrNull { it.startsWith("rank class") }
        val row = after.firstOrNull { it.startsWith("1 (") }
        val map = LinkedHashMap<String, Double>()
        if (header != null && row != null) {
            val names = header.trim().split(Regex("\\s+")).drop(2).takeWhile { it != "peakV" }
            val vals = row.trim().split(Regex("\\s+")).drop(3)
            for ((i, nm) in names.withIndex()) vals.getOrNull(i)?.toDoubleOrNull()?.let { map[nm] = it }
        }
        bestProfile = map
        discovery.visibility = View.VISIBLE
        discovery.alpha = 0f; discovery.translationY = dpf(30f)
        discovery.animate().alpha(1f).translationY(0f).setDuration(480).setInterpolator(DecelerateInterpolator(2f)).start()
        ValueAnimator.ofInt(0, hits).apply {
            duration = 900; interpolator = DecelerateInterpolator(2f)
            addUpdateListener { discCount.text = "${it.animatedValue} / $total" }
            start()
        }
        discCount.setTextColor(if (hits > 0) pal.green else pal.orange)
        discBest.text = if (map.isEmpty()) "—" else map.entries.joinToString("\n") { e ->
            val label = params.firstOrNull { it.name == e.key }?.label ?: e.key
            label.padEnd(18) + fmt(e.value)
        }
        discReport.text = rep
        selectSeg(true)
        discovery.post { (discovery.parent as? ScrollView)?.smoothScrollTo(0, discovery.top - dp(60)) }
        discoverBtn.confirmHaptic()
    }
}
