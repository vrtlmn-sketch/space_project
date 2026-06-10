package handlers

import (
	"net/http"

	"github.com/vrtlmn-sketch/space_project/web/internal/views"
)

func Home(r *views.Renderer) http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		if err := r.Render(w, "home.html", nil); err != nil {
			http.Error(w, "internal error", http.StatusInternalServerError)
		}
	}
}
