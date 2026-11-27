package main

import (
	"embed"

	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
	"github.com/wailsapp/wails/v2/pkg/options/linux"
	"github.com/wailsapp/wails/v2/pkg/options/windows"
)

//go:embed all:frontend/dist
var assets embed.FS

func main() {
	app := NewApp()

	err := wails.Run(&options.App{
		Title:            "OLY Persona",
		Width:            1280,
		Height:           800,
		MinWidth:         960,
		MinHeight:        640,
		Frameless:        false,
		BackgroundColour: &options.RGBA{R: 10, G: 10, B: 15, A: 1},
		AssetServer: &assetserver.Options{
			Assets: assets,
		},
		OnStartup:  app.startup,
		OnShutdown: app.shutdown,
		Bind: []interface{}{
			app,
		},
		Windows: &windows.Options{
			WebviewIsTransparent: false,
			WindowIsTranslucent:  false,
			DisableWindowIcon:    false,
			DisablePinchZoom:     false,
			EnableSwipeGestures:  true,
			Theme:                windows.Dark,
		},
		Linux: &linux.Options{
			ProgramName: "oly-persona",
		},
	})
	if err != nil {
		panic(err)
	}
}
