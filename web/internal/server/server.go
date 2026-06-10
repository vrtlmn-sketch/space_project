package server

import (
	"log/slog"
	"net/http"

	"github.com/vrtlmn-sketch/space_project/web/internal/handlers"
	"github.com/vrtlmn-sketch/space_project/web/internal/views"
)

func New(logger *slog.Logger) http.Handler {
	mux := http.NewServeMux()

	renderer := views.NewRenderer()

	mux.HandleFunc("GET /{$}", handlers.Home(renderer))

	mux.Handle("GET /static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))

	return mux
}
