package views

import (
	"html/template"
	"net/http"
	"path/filepath"
)

const viewsDir = "internal/views"

type Renderer struct{}

func NewRenderer() *Renderer {
	return &Renderer{}
}

func (r *Renderer) Render(w http.ResponseWriter, page string, data any) error {
	tmpl, err := template.ParseFiles(
		filepath.Join(viewsDir, "layouts", "base.html"),
		filepath.Join(viewsDir, "pages", page),
	)
	if err != nil {
		return err
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	return tmpl.ExecuteTemplate(w, "base", data)
}

func (r *Renderer) RenderPartial(w http.ResponseWriter, partial string, data any) error {
	tmpl, err := template.ParseFiles(
		filepath.Join(viewsDir, "partials", partial),
	)
	if err != nil {
		return err
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	return tmpl.ExecuteTemplate(w, partial, data)
}
