package com.example.ebovtip

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.Configuration
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.animation.OvershootInterpolator

/** iOS-style colour system with automatic light / dark mode. */
class Palette(ctx: Context) {
    val dark = (ctx.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
    val bg = if (dark) 0xFF000000.toInt() else 0xFFF2F2F7.toInt()
    val card = if (dark) 0xFF1C1C1E.toInt() else 0xFFFFFFFF.toInt()
    val card2 = if (dark) 0xFF2C2C2E.toInt() else 0xFFEFEFF4.toInt()
    val text = if (dark) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
    val sub = if (dark) 0xFF8E8E93.toInt() else 0xFF6C6C70.toInt()
    val sep = if (dark) 0xFF3A3A3C.toInt() else 0xFFD1D1D6.toInt()
    val blue = 0xFF0A84FF.toInt()
    val green = 0xFF30D158.toInt()
    val red = 0xFFFF453A.toInt()
    val orange = 0xFFFF9F0A.toInt()
    val purple = 0xFFBF5AF2.toInt()
    val teal = 0xFF64D2FF.toInt()
}

fun Context.dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
fun Context.dpf(v: Float): Float = v * resources.displayMetrics.density

fun roundRect(color: Int, radiusPx: Float): GradientDrawable =
    GradientDrawable().apply { setColor(color); cornerRadius = radiusPx }

fun View.tick() { performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK) }

fun View.confirmHaptic() {
    performHapticFeedback(if (Build.VERSION.SDK_INT >= 30) HapticFeedbackConstants.CONFIRM else HapticFeedbackConstants.LONG_PRESS)
}

/** Spring-like press feedback (shrinks on touch, overshoots back on release) plus a light haptic tick. */
@SuppressLint("ClickableViewAccessibility")
fun View.pressable(onClick: () -> Unit) {
    setOnTouchListener { v, e ->
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN ->
                v.animate().scaleX(0.96f).scaleY(0.96f).setDuration(90).start()
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                v.animate().scaleX(1f).scaleY(1f).setDuration(300).setInterpolator(OvershootInterpolator(3f)).start()
                if (e.actionMasked == MotionEvent.ACTION_UP && e.x >= 0f && e.x <= v.width && e.y >= 0f && e.y <= v.height) {
                    v.tick(); onClick()
                }
            }
        }
        true
    }
}

/** iOS-style slider: thin rounded track, white thumb with shadow, haptic ticks. value is 0..1. */
class IosSlider(ctx: Context, private val pal: Palette) : View(ctx) {
    var onChange: ((Float) -> Unit)? = null
    var value: Float = 0f
        set(v) { field = v.coerceIn(0f, 1f); invalidate(); onChange?.invoke(field) }

    private val r = ctx.dpf(14f)
    private val trackH = ctx.dpf(5f)
    private val pTrack = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = pal.sep }
    private val pFill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = pal.blue }
    private val pThumb = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE }
    private var lastBucket = -1

    init {
        setLayerType(LAYER_TYPE_SOFTWARE, null)
        pThumb.setShadowLayer(ctx.dpf(5f), 0f, ctx.dpf(2f), 0x55000000)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        setMeasuredDimension(resolveSize(context.dp(200), widthMeasureSpec), context.dp(36))
    }

    override fun onDraw(c: Canvas) {
        val w = width.toFloat()
        val cy = height / 2f
        val cx = r + value * (w - 2 * r)
        c.drawRoundRect(r, cy - trackH / 2, w - r, cy + trackH / 2, trackH, trackH, pTrack)
        c.drawRoundRect(r, cy - trackH / 2, cx, cy + trackH / 2, trackH, trackH, pFill)
        c.drawCircle(cx, cy, r - context.dpf(1f), pThumb)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                value = ((e.x - r) / (width - 2 * r)).coerceIn(0f, 1f)
                val b = (value * 20).toInt()
                if (b != lastBucket) { lastBucket = b; tick() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> parent?.requestDisallowInterceptTouchEvent(false)
        }
        return true
    }
}
