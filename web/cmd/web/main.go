package main

import (
	"log/slog"
	"net/http"
	"os"

	"github.com/vrtlmn-sketch/space_project/web/internal/server"
)

func main() {
	logger := slog.New(slog.NewTextHandler(os.Stdout, nil))

	addr := ":8080"
	logger.Info("starting server", "addr", addr)

	if err := http.ListenAndServe(addr, server.New(logger)); err != nil {
		logger.Error("server stopped", "err", err)
		os.Exit(1)
	}
}
