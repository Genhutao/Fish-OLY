package com.oly.world.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.lifecycle.viewmodel.compose.viewModel
import com.oly.world.data.ServerConfig

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val config = ServerConfig(this)
        setContent {
            OlyTheme {
                App(viewModel(factory = AppViewModel.factory(config)))
            }
        }
    }
}
