// SPDX-License-Identifier: MIT
package main

import (
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"regexp"
	"strings"
	"sync"
	"time"
)

const (
	sessionTTL       = 5 * time.Minute
	cleanupInterval  = 30 * time.Second
	maxPayloadBytes  = 8192
	maxActiveSession = 10000
	maxCreatesMinute = 30
)

//go:embed static/pair.js
var staticFiles embed.FS

var sessionIDPattern = regexp.MustCompile(`^[a-f0-9]{32}$`)

type encryptedPayload struct {
	IV         string `json:"iv"`
	Ciphertext string `json:"ciphertext"`
}

type session struct {
	expiresAt time.Time
	payload   *encryptedPayload
}

type rateWindow struct {
	start time.Time
	count int
}

type relay struct {
	mu       sync.Mutex
	sessions map[string]*session
	rates    map[string]*rateWindow
}

func newRelay() *relay {
	return &relay{
		sessions: make(map[string]*session),
		rates:    make(map[string]*rateWindow),
	}
}

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
		next.ServeHTTP(w, r)
	})
}

func clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}

	peer := net.ParseIP(host)
	if peer != nil && peer.IsLoopback() {
		if realIP := strings.TrimSpace(r.Header.Get("X-Real-IP")); net.ParseIP(realIP) != nil {
			return realIP
		}
	}
	return host
}

func (s *relay) allowSessionCreate(ip string, now time.Time) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	window := s.rates[ip]
	if window == nil || now.Sub(window.start) >= time.Minute {
		s.rates[ip] = &rateWindow{start: now, count: 1}
		return true
	}
	if window.count >= maxCreatesMinute {
		return false
	}
	window.count++
	return true
}

func parseSessionPath(path, suffix string) (string, bool) {
	const prefix = "/api/v1/sessions/"
	if !strings.HasPrefix(path, prefix) {
		return "", false
	}
	rest := strings.TrimPrefix(path, prefix)
	if suffix != "" {
		if !strings.HasSuffix(rest, suffix) {
			return "", false
		}
		rest = strings.TrimSuffix(rest, suffix)
	}
	if !sessionIDPattern.MatchString(rest) {
		return "", false
	}
	return rest, true
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	if value != nil {
		_ = json.NewEncoder(w).Encode(value)
	}
}

func (s *relay) createSession(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	now := time.Now()
	if !s.allowSessionCreate(clientIP(r), now) {
		http.Error(w, "rate limit exceeded", http.StatusTooManyRequests)
		return
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.sessions) >= maxActiveSession {
		http.Error(w, "relay busy", http.StatusServiceUnavailable)
		return
	}
	if existing := s.sessions[id]; existing != nil && now.Before(existing.expiresAt) {
		http.Error(w, "session already exists", http.StatusConflict)
		return
	}
	s.sessions[id] = &session{expiresAt: now.Add(sessionTTL)}
	writeJSON(w, http.StatusCreated, map[string]any{"expires_in": int(sessionTTL.Seconds())})
}

func decodePayload(r *http.Request) (*encryptedPayload, error) {
	body := http.MaxBytesReader(nil, r.Body, maxPayloadBytes)
	defer body.Close()
	decoder := json.NewDecoder(body)
	decoder.DisallowUnknownFields()

	var payload encryptedPayload
	if err := decoder.Decode(&payload); err != nil {
		return nil, err
	}
	if payload.IV == "" || payload.Ciphertext == "" {
		return nil, errors.New("missing encrypted fields")
	}
	if len(payload.IV) > 64 || len(payload.Ciphertext) > maxPayloadBytes {
		return nil, errors.New("encrypted payload too large")
	}
	if err := decoder.Decode(&struct{}{}); err != io.EOF {
		return nil, errors.New("unexpected trailing data")
	}
	return &payload, nil
}

func (s *relay) submitPayload(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	payload, err := decodePayload(r)
	if err != nil {
		http.Error(w, "invalid payload", http.StatusBadRequest)
		return
	}

	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	entry := s.sessions[id]
	if entry == nil || !now.Before(entry.expiresAt) {
		delete(s.sessions, id)
		http.Error(w, "session expired", http.StatusGone)
		return
	}
	if entry.payload != nil {
		http.Error(w, "payload already submitted", http.StatusConflict)
		return
	}
	entry.payload = payload
	w.WriteHeader(http.StatusNoContent)
}

func (s *relay) getPayload(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	now := time.Now()
	s.mu.Lock()
	defer s.mu.Unlock()
	entry := s.sessions[id]
	if entry == nil || !now.Before(entry.expiresAt) {
		delete(s.sessions, id)
		http.Error(w, "session expired", http.StatusGone)
		return
	}
	if entry.payload == nil {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	writeJSON(w, http.StatusOK, entry.payload)
}

func (s *relay) deleteSession(w http.ResponseWriter, r *http.Request, id string) {
	if r.Method != http.MethodDelete {
		w.Header().Set("Allow", http.MethodDelete)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	s.mu.Lock()
	delete(s.sessions, id)
	s.mu.Unlock()
	w.WriteHeader(http.StatusNoContent)
}

func (s *relay) api(w http.ResponseWriter, r *http.Request) {
	if id, ok := parseSessionPath(r.URL.Path, "/payload"); ok {
		if r.Method == http.MethodPost {
			s.submitPayload(w, r, id)
		} else {
			s.getPayload(w, r, id)
		}
		return
	}
	id, ok := parseSessionPath(r.URL.Path, "")
	if !ok {
		http.NotFound(w, r)
		return
	}
	if r.Method == http.MethodDelete {
		s.deleteSession(w, r, id)
		return
	}
	s.createSession(w, r, id)
}

const pairHTML = `<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blazzing · Adicionar playlist</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#070a12;color:#f6f8fc;font-family:system-ui,-apple-system,sans-serif;padding:24px}
.card{max-width:560px;margin:7vh auto;background:#0e1420;border:1px solid #2b3950;border-radius:18px;padding:24px;box-shadow:0 20px 70px #0008}
h1{font-size:24px;margin:0 0 8px}.sub{color:#91a0b7;margin:0 0 24px;line-height:1.5}
label{display:block;margin:18px 0 7px;color:#b9c5d6;font-size:14px}
input{width:100%;padding:14px;border-radius:11px;border:1px solid #43536d;background:#151e2d;color:#fff;font-size:16px;outline:none}
input:focus{border-color:#62a9ff;box-shadow:0 0 0 3px #62a9ff33}
button{width:100%;margin-top:22px;padding:14px;border:0;border-radius:11px;background:#62a9ff;color:#07101d;font-weight:750;font-size:16px;cursor:pointer}
button:disabled{opacity:.55;cursor:wait}.status{min-height:24px;margin-top:16px;color:#91a0b7}.ok{color:#7ee787}.err{color:#ff7185}
small{display:block;color:#66758b;margin-top:22px;line-height:1.45}
</style>
<script src="/static/pair.js" defer></script>
</head>
<body>
<main class="card">
<h1>Adicionar playlist ao Blazzing</h1>
<p class="sub">Cole a URL aqui. O navegador cifra os dados antes de enviá-los ao relay.</p>
<label for="name">Nome da lista (opcional)</label>
<input id="name" maxlength="127" autocomplete="off">
<label for="url">URL M3U/M3U8</label>
<input id="url" type="url" maxlength="511" required autofocus placeholder="https://.../lista.m3u8">
<button id="send" type="utton">Enviar para o Blazzing</button>
<div id="status" class="status" aria-live="polite"></div>
<small>A chave de criptografia fica no fragmento # do QR e não é incluída nas requisições HTTP normais.</small>
</main>
</body>
</html>`

func pairPage(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/pair/")
	if !sessionIDPattern.MatchString(id) {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Content-Security-Policy",
		"default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; "+
			"img-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'")
	_, _ = io.WriteString(w, pairHTML)
}

func staticJS(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet || r.URL.Path != "/static/pair.js" {
		http.NotFound(w, r)
		return
	}
	data, err := staticFiles.ReadFile("static/pair.js")
	if err != nil {
		http.Error(w, "asset unavailable", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/javascript; charset=utf-8")
	w.Header().Set("Content-Security-Policy", "default-src 'none'")
	_, _ = w.Write(data)
}

func (s *relay) cleanupLoop() {
	ticker := time.NewTicker(cleanupInterval)
	defer ticker.Stop()
	for now := range ticker.C {
		s.mu.Lock()
		for id, entry := range s.sessions {
			if !now.Before(entry.expiresAt) {
				delete(s.sessions, id)
			}
		}
		for ip, window := range s.rates {
			if now.Sub(window.start) > 2*time.Minute {
				delete(s.rates, ip)
			}
		}
		s.mu.Unlock()
	}
}

func relayHandler(app *relay) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})
	mux.HandleFunc("/api/v1/sessions/", app.api)
	mux.HandleFunc("/pair/", pairPage)
	mux.HandleFunc("/static/pair.js", staticJS)
	return securityHeaders(mux)
}

func main() {
	addr := os.Getenv("BLZ_RELAY_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8080"
	}

	app := newRelay()
	go app.cleanupLoop()

	server := &http.Server{
		Addr:              addr,
		Handler:           relayHandler(app),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       30 * time.Second,
	}

	log.Printf("Blazzing pairing relay listening on %s", addr)
	if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(fmt.Errorf("relay server: %w", err))
	}
}
