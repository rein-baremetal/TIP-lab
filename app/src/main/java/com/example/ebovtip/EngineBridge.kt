package com.example.ebovtip

/** Thin Kotlin wrapper over the C++ engine. No science lives here. */
object EngineBridge {
    init { System.loadLibrary("ebovengine") }

    private external fun version(): String
    private external fun run(params: String, format: Int): String
    private external fun screen(params: String, screenCfg: String, prefix: String): String

    fun engineVersion(): String = version()

    /** Params text = "name = value" lines, same names as engine/input.txt. format: 0 summary, 1 table, 2 SVG. */
    fun runRaw(paramsText: String, format: Int): String = run(paramsText, format)

    /** Writes <prefix>_report.txt, _samples.csv, _pareto.svg, _best.svg and returns the report text. Run off the UI thread. */
    fun screenRaw(paramsText: String, screenCfgText: String, prefix: String): String =
        screen(paramsText, screenCfgText, prefix)
}
