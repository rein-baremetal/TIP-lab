package com.example.ebovtip

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin
import kotlin.random.Random

/** One simulated run: time axis, log10 virus (treated), log10 TIP, log10 virus (untreated ghost), cytokine, pulse. */
class SimData(
    val t: FloatArray, val lvW: FloatArray, val lvT: FloatArray,
    val ghost: FloatArray, val c: FloatArray, val h: FloatArray
)

/**
 * The hero "arena": plays the simulation back as an animated game screen.
 * Floating virus (red) and TIP (blue) particles scale with the live load, meters track virus / cytokine / pulse,
 * curves draw themselves with a glowing playhead, and a victory banner with stars and confetti fires on eradication.
 * Tap = pause/replay, drag = scrub.
 */
class SimView(ctx: Context) : View(ctx) {
    var onFinished: ((Boolean) -> Unit)? = null

    private var data: SimData? = null
    private var extinct = false
    private var subtitle = ""
    private var flags = BooleanArray(3)
    private val starLabels = arrayOf("Cleared", "Safe", "Fast")
    private val cCrit = 500f
    private val hRest = 70f
    private val hCrit = 130f

    private var u = 0f
    private var playing = false
    private var scrubbing = false
    private var sinceEnd = -1f
    private var lastNs = 0L
    private var downX = 0f
    private var downY = 0f
    private var moved = false
    private var lastDay = -1
    private val durationMs = 7500f

    private var dv = 0f
    private var dc = 0f
    private var dh = 0f

    private class Dot(var x: Float, var y: Float, var vx: Float, var vy: Float, var ph: Float, var a: Float, var r: Float)
    private class Piece(var x: Float, var y: Float, var vx: Float, var vy: Float, var rot: Float, var vr: Float, val color: Int, var life: Float)

    private val rnd = Random(11)
    private val virus = List(70) { newDot(0.9f, 3.2f) }
    private val tips = List(44) { newDot(0.8f, 2.6f) }
    private val confetti = ArrayList<Piece>()

    private fun newDot(vmax: Float, rmax: Float) = Dot(
        rnd.nextFloat(), rnd.nextFloat(), (rnd.nextFloat() - 0.5f) * 0.05f * vmax, (rnd.nextFloat() - 0.5f) * 0.05f * vmax,
        rnd.nextFloat() * 6.28f, 0f, 1.2f + rnd.nextFloat() * rmax
    )

    // paints
    private val pBg = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pGlow = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pFill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    private val pText = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pGrid = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x22FFFFFF; strokeWidth = 1f }
    private val sansBold = Typeface.create("sans-serif", Typeface.BOLD)
    private val sansMed = Typeface.create("sans-serif-medium", Typeface.NORMAL)
    private val sansBlack = Typeface.create("sans-serif-black", Typeface.NORMAL)

    private val cVirus = 0xFFFF6B5E.toInt()
    private val cTip = 0xFF64D2FF.toInt()
    private val cGood = 0xFF30D158.toInt()
    private val cWarn = 0xFFFF9F0A.toInt()
    private val cBad = 0xFFFF453A.toInt()
    private val gold = 0xFFFFD60A.toInt()

    private val bgPath = Path()
    private var bgShader: Shader? = null
    private var glowShader: Shader? = null

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        bgShader = LinearGradient(0f, 0f, 0f, h.toFloat(), 0xFF0B1226.toInt(), 0xFF1A2B52.toInt(), Shader.TileMode.CLAMP)
        glowShader = RadialGradient(w * 0.85f, h * 0.05f, w * 0.7f, 0x440A84FF, 0x000A84FF, Shader.TileMode.CLAMP)
        bgPath.reset()
        val r = context.dpf(28f)
        bgPath.addRoundRect(RectF(0f, 0f, w.toFloat(), h.toFloat()), r, r, Path.Direction.CW)
    }

    fun show(d: SimData, isExtinct: Boolean, sub: String, starFlags: BooleanArray) {
        data = d; extinct = isExtinct; subtitle = sub; flags = starFlags
        replay()
    }

    fun replay() {
        u = 0f; playing = true; scrubbing = false; sinceEnd = -1f; confetti.clear(); lastDay = -1
        invalidate()
    }

    private fun dp(v: Float) = context.dpf(v)

    private fun sample(a: FloatArray, p: Float): Float {
        if (a.size < 2) return a.firstOrNull() ?: 0f
        val f = p * (a.size - 1)
        val i = f.toInt().coerceIn(0, a.size - 2)
        val fr = f - i
        return a[i] * (1 - fr) + a[i + 1] * fr
    }

    private fun lerpColor(a: Int, b: Int, t: Float): Int {
        val k = t.coerceIn(0f, 1f)
        return Color.argb(
            (Color.alpha(a) + (Color.alpha(b) - Color.alpha(a)) * k).toInt(),
            (Color.red(a) + (Color.red(b) - Color.red(a)) * k).toInt(),
            (Color.green(a) + (Color.green(b) - Color.green(a)) * k).toInt(),
            (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * k).toInt()
        )
    }

    private fun danger(f: Float): Int = if (f < 0.5f) lerpColor(cGood, cWarn, f / 0.5f) else lerpColor(cWarn, cBad, (f - 0.5f) / 0.5f)

    private fun overshoot(x: Float): Float {
        val k = x.coerceIn(0f, 1f); val s = 1.70158f; val y = k - 1f
        return y * y * ((s + 1f) * y + s) + 1f
    }

    private fun spawnConfetti() {
        val colors = intArrayOf(cGood, cTip, gold, 0xFFBF5AF2.toInt(), cVirus, Color.WHITE)
        repeat(90) {
            val ang = (rnd.nextFloat() * 3.1416f) + 3.1416f
            val sp = 220f + rnd.nextFloat() * 520f
            confetti.add(Piece(width / 2f, height * 0.42f, cos(ang) * sp, sin(ang) * sp, rnd.nextFloat() * 6f, (rnd.nextFloat() - 0.5f) * 14f, colors[rnd.nextInt(colors.size)], 2.4f + rnd.nextFloat()))
        }
    }

    private fun advance(dt: Float) {
        val d = data
        if (d != null && playing && !scrubbing) {
            u += dt * 1000f / durationMs
            if (u >= 1f) {
                u = 1f; playing = false; sinceEnd = 0f
                if (extinct) spawnConfetti()
                onFinished?.invoke(extinct)
            }
        } else if (sinceEnd >= 0f) sinceEnd += dt
        for (p in confetti) { p.vy += 900f * dt; p.x += p.vx * dt; p.y += p.vy * dt; p.rot += p.vr * dt; p.life -= dt }
        confetti.removeAll { it.life <= 0f || it.y > height + 40f }
    }

    override fun onDraw(c: Canvas) {
        val now = System.nanoTime()
        val dt = if (lastNs == 0L) 0.016f else min(0.05f, (now - lastNs) / 1e9f)
        lastNs = now
        advance(dt)

        val w = width.toFloat(); val h = height.toFloat()
        c.save(); c.clipPath(bgPath)
        pBg.shader = bgShader; c.drawRect(0f, 0f, w, h, pBg)
        pBg.shader = glowShader; c.drawRect(0f, 0f, w, h, pBg); pBg.shader = null

        val d = data
        val p = if (d != null) u else 0f
        val lvNow = if (d != null) sample(d.lvW, p) else 6f
        val tipNow = if (d != null) sample(d.lvT, p) else -4f
        drawParticles(c, w, h, dt, lvNow, tipNow, d != null)

        if (d == null || d.t.size < 2) {
            pText.typeface = sansMed; pText.color = 0xCCFFFFFF.toInt(); pText.textSize = dp(16f); pText.textAlign = Paint.Align.CENTER
            c.drawText("Choose a TIP profile and press Run", w / 2, h / 2, pText)
            c.restore(); postInvalidateOnAnimation(); return
        }

        val tEnd = d.t.last()
        val day = p * tEnd
        // ---- HUD
        pText.textAlign = Paint.Align.LEFT; pText.letterSpacing = 0.12f
        pText.typeface = sansBold; pText.textSize = dp(10.5f); pText.color = 0x99FFFFFF.toInt()
        c.drawText("DAY", dp(20f), dp(30f), pText)
        pText.letterSpacing = 0f; pText.typeface = sansBlack; pText.textSize = dp(46f); pText.color = Color.WHITE
        c.drawText(String.format(java.util.Locale.US, "%.1f", day), dp(20f), dp(78f), pText)

        val vT = ((lvNow + 4f) / 12f).coerceIn(0f, 1f)
        val cT = (sample(d.c, p) / cCrit).coerceIn(0f, 1f)
        val hT = ((sample(d.h, p) - hRest) / (hCrit - hRest)).coerceIn(0f, 1f)
        val k = min(1f, dt * 8f)
        dv += (vT - dv) * k; dc += (cT - dc) * k; dh += (hT - dh) * k
        val bx = dp(112f); val bw = w - bx - dp(20f)
        drawBar(c, "VIRUS", bx, dp(24f), bw, dv, cVirus)
        drawBar(c, "CYTOKINE", bx, dp(50f), bw, dc, danger(dc))
        drawBar(c, "PULSE", bx, dp(76f), bw, dh, danger(dh))

        // ---- chart
        val cl = dp(18f); val cr = w - dp(18f); val ct = dp(112f); val cb = h - dp(30f)
        var ymax = 8f
        for (a in arrayOf(d.lvW, d.lvT, d.ghost)) for (v in a) ymax = max(ymax, ceil(v))
        val ymin = -4f
        fun px(t: Float) = cl + t / tEnd * (cr - cl)
        fun py(v: Float) = cb - (v - ymin) / (ymax - ymin) * (cb - ct)
        var g = ymin
        pText.textSize = dp(9.5f); pText.typeface = sansMed; pText.color = 0x66FFFFFF.toInt()
        while (g <= ymax) {
            c.drawLine(cl, py(g), cr, py(g), pGrid)
            c.drawText("1e" + g.toInt(), cl + dp(2f), py(g) - dp(3f), pText)
            g += 4f
        }
        // clear line
        pLine.pathEffect = DashPathEffect(floatArrayOf(dp(6f), dp(6f)), 0f)
        pLine.strokeWidth = dp(1.2f); pLine.color = 0xAA30D158.toInt()
        val yExt = py(log10(2e-4f))
        c.drawLine(cl, yExt, cr, yExt, pLine)
        pText.textAlign = Paint.Align.RIGHT; pText.color = 0xCC30D158.toInt(); pText.textSize = dp(10f)
        c.drawText("eradication line", cr, yExt - dp(4f), pText)
        pText.textAlign = Paint.Align.LEFT

        curve(c, d.t, d.ghost, 1f, ::px, ::py, 0x88FFFFFF.toInt(), 1.6f, true)
        curve(c, d.t, d.lvT, p, ::px, ::py, cTip, 2.2f, false)
        curve(c, d.t, d.lvW, p, ::px, ::py, cVirus, 2.6f, false)

        // playhead
        val hx = px(day)
        pLine.pathEffect = null; pLine.strokeWidth = dp(1f); pLine.color = 0x33FFFFFF
        c.drawLine(hx, ct, hx, cb, pLine)
        val hy = py(lvNow)
        val pulse = 1f + 0.35f * sin(now / 1e8f)
        pFill.color = 0x33FF6B5E; c.drawCircle(hx, hy, dp(10f) * pulse, pFill)
        pFill.color = Color.WHITE; c.drawCircle(hx, hy, dp(4.5f), pFill)

        // legend
        pText.textSize = dp(10.5f); pText.color = cVirus; c.drawText("● virus", cl, h - dp(10f), pText)
        pText.color = cTip; c.drawText("● TIP", cl + dp(62f), h - dp(10f), pText)
        pText.color = 0x99FFFFFF.toInt(); c.drawText("– – untreated", cl + dp(112f), h - dp(10f), pText)
        pText.textAlign = Paint.Align.RIGHT; pText.color = 0x66FFFFFF.toInt()
        c.drawText("tap to pause · drag to scrub", cr, h - dp(10f), pText)
        pText.textAlign = Paint.Align.LEFT

        if (sinceEnd >= 0f) drawVerdict(c, w, h)
        drawConfetti(c)
        c.restore()
        postInvalidateOnAnimation()
    }

    private fun drawBar(c: Canvas, label: String, x: Float, y: Float, w: Float, frac: Float, color: Int) {
        pText.textAlign = Paint.Align.LEFT; pText.letterSpacing = 0.1f; pText.typeface = sansBold
        pText.textSize = dp(9.5f); pText.color = 0x99FFFFFF.toInt()
        c.drawText(label, x, y + dp(7f), pText)
        pText.letterSpacing = 0f
        val lx = x + dp(62f); val bw = w - dp(62f); val bh = dp(8f)
        pFill.color = 0x22FFFFFF; c.drawRoundRect(lx, y, lx + bw, y + bh, bh, bh, pFill)
        val fw = max(bh, bw * frac)
        pFill.color = color; c.drawRoundRect(lx, y, lx + fw, y + bh, bh, bh, pFill)
    }

    private fun curve(c: Canvas, t: FloatArray, a: FloatArray, p: Float, px: (Float) -> Float, py: (Float) -> Float, color: Int, width: Float, dashed: Boolean) {
        if (a.size < 2) return
        val f = p * (a.size - 1)
        val last = f.toInt().coerceIn(0, a.size - 2)
        val path = Path()
        path.moveTo(px(t[0]), py(a[0]))
        for (i in 1..last) path.lineTo(px(t[i]), py(a[i]))
        val fr = f - last
        path.lineTo(px(t[last] + (t[last + 1] - t[last]) * fr), py(a[last] + (a[last + 1] - a[last]) * fr))
        pLine.pathEffect = if (dashed) DashPathEffect(floatArrayOf(dp(7f), dp(7f)), 0f) else null
        if (!dashed) {
            pLine.color = (color and 0x00FFFFFF) or 0x30000000; pLine.strokeWidth = dp(width * 3.2f); c.drawPath(path, pLine)
            pLine.color = (color and 0x00FFFFFF) or 0x66000000; pLine.strokeWidth = dp(width * 1.8f); c.drawPath(path, pLine)
        }
        pLine.color = color; pLine.strokeWidth = dp(width); c.drawPath(path, pLine)
        pLine.pathEffect = null
    }

    private fun drawParticles(c: Canvas, w: Float, h: Float, dt: Float, lv: Float, tipLv: Float, active: Boolean) {
        val vCount = if (active) ((lv + 4f) / 11f).coerceIn(0f, 1f) * virus.size else virus.size * 0.5f
        val tCount = if (active) ((tipLv + 4f) / 13f).coerceIn(0f, 1f) * tips.size else 0f
        fun step(list: List<Dot>, count: Float, color: Int, drift: Float) {
            for ((i, dd) in list.withIndex()) {
                dd.ph += dt * 1.3f
                dd.x += (dd.vx + sin(dd.ph) * 0.01f * drift) * dt * 6f
                dd.y += (dd.vy + cos(dd.ph * 0.8f) * 0.01f * drift) * dt * 6f
                if (dd.x < 0f) dd.x += 1f; if (dd.x > 1f) dd.x -= 1f
                if (dd.y < 0f) dd.y += 1f; if (dd.y > 1f) dd.y -= 1f
                val target = if (i < count) 0.55f else 0f
                dd.a += (target - dd.a) * min(1f, dt * 3f)
                if (dd.a > 0.02f) {
                    pFill.color = (color and 0x00FFFFFF) or ((dd.a * 255).toInt() shl 24)
                    c.drawCircle(dd.x * w, dd.y * h, dp(dd.r), pFill)
                }
            }
        }
        step(virus, vCount, cVirus, 1f)
        step(tips, tCount, cTip, 1.4f)
    }

    private fun drawStar(c: Canvas, cx: Float, cy: Float, r: Float, filled: Boolean) {
        val path = Path()
        for (i in 0 until 10) {
            val ang = -1.5708f + i * 0.62832f
            val rad = if (i % 2 == 0) r else r * 0.45f
            val x = cx + cos(ang) * rad; val y = cy + sin(ang) * rad
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        path.close()
        if (filled) { pFill.color = gold; c.drawPath(path, pFill) }
        else { pLine.pathEffect = null; pLine.color = 0x55FFFFFF; pLine.strokeWidth = dp(1.6f); c.drawPath(path, pLine) }
    }

    private fun drawVerdict(c: Canvas, w: Float, h: Float) {
        val a = (sinceEnd / 0.3f).coerceIn(0f, 1f)
        val s = overshoot(sinceEnd / 0.7f)
        val cx = w / 2f; val cy = h * 0.5f
        pFill.color = ((a * 150).toInt() shl 24) or 0x000814
        c.drawRoundRect(cx - w * 0.4f, cy - dp(78f), cx + w * 0.4f, cy + dp(92f), dp(24f), dp(24f), pFill)
        c.save(); c.scale(s, s, cx, cy)
        val title = if (extinct) "ERADICATED" else "VIRUS PERSISTS"
        val tc = if (extinct) cGood else cWarn
        pText.textAlign = Paint.Align.CENTER; pText.typeface = sansBlack; pText.letterSpacing = 0.04f
        pText.textSize = dp(30f)
        pText.color = (tc and 0x00FFFFFF) or ((a * 70).toInt() shl 24); c.drawText(title, cx, cy - dp(22f) + dp(2f), pText)
        pText.color = (tc and 0x00FFFFFF) or ((a * 255).toInt() shl 24); c.drawText(title, cx, cy - dp(24f), pText)
        pText.letterSpacing = 0f; pText.typeface = sansMed; pText.textSize = dp(14f)
        pText.color = (0xFFFFFF) or ((a * 220).toInt() shl 24); c.drawText(subtitle, cx, cy - dp(2f), pText)
        c.restore()
        for (i in 0 until 3) {
            val st = ((sinceEnd - 0.5f - i * 0.25f) / 0.45f)
            if (st <= 0f) continue
            val sc = overshoot(st)
            val sx = cx + (i - 1) * dp(66f); val sy = cy + dp(38f)
            c.save(); c.scale(sc, sc, sx, sy)
            drawStar(c, sx, sy, dp(18f), flags[i])
            pText.typeface = sansMed; pText.textSize = dp(10.5f); pText.color = if (flags[i]) 0xFFFFFFFF.toInt() else 0x77FFFFFF
            c.drawText(starLabels[i], sx, sy + dp(34f), pText)
            c.restore()
        }
        pText.textAlign = Paint.Align.LEFT
    }

    private fun drawConfetti(c: Canvas) {
        for (q in confetti) {
            c.save(); c.rotate(q.rot * 57.3f, q.x, q.y)
            pFill.color = (q.color and 0x00FFFFFF) or (min(1f, q.life) * 255).toInt().shl(24)
            c.drawRect(q.x - dp(3f), q.y - dp(2f), q.x + dp(3f), q.y + dp(2f), pFill)
            c.restore()
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val d = data ?: return false
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; downY = e.y; moved = false; return true }
            MotionEvent.ACTION_MOVE -> {
                if (!moved && abs(e.x - downX) > dp(12f) && abs(e.x - downX) > abs(e.y - downY)) {
                    moved = true; scrubbing = true; playing = false; sinceEnd = -1f
                    parent?.requestDisallowInterceptTouchEvent(true)
                }
                if (moved) {
                    val cl = dp(18f); val cr = width - dp(18f)
                    u = ((e.x - cl) / (cr - cl)).coerceIn(0f, 1f)
                    val day = (u * d.t.last()).toInt()
                    if (day != lastDay) { lastDay = day; tick() }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                if (moved) {
                    scrubbing = false
                    if (u < 1f) playing = true else { playing = false; sinceEnd = 0f }
                } else if (e.actionMasked == MotionEvent.ACTION_UP) {
                    if (sinceEnd >= 0f || u >= 1f) replay() else playing = !playing
                    tick()
                }
            }
        }
        return true
    }
}
