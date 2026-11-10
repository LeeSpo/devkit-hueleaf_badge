package com.hueleaf.remote

import android.app.Application
import com.google.android.material.color.DynamicColors

class HueLeafApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        DynamicColors.applyToActivitiesIfAvailable(this)
    }
}
