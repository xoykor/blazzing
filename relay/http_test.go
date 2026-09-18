// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func request(t *testing.T, client *http.Client, method, url, body string) *http.Response {
	t.Helper()
	var reader io.Reader
	if body != "" {
		reader = bytes.NewBufferString(body)
	}
	req, err := http.NewRequest(method, url, reader)
	if err != nil {
		t.Fatal(err)
	}
	if body != "" {
		req.Header.Set("Content-Type", "application/json")
	}
	resp, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	return resp
}

func TestHTTPRelayLifecycle(t *testing.T) {
	app := newRelay()
	server := httptest.NewServer(relayHandler(app))
	defer server.Close()

	client := server.Client()
	id := "0123456789abcdef0123456789abcdef"

	resp := request(t, client, http.MethodPost, server.URL+"/api/v1/sessions/"+id, "")
	if resp.StatusCode != http.StatusCreated {
		t.Fatalf("create status = %d", resp.StatusCode)
	}
	resp.Body.Close()

	resp = request(t, client, http.MethodGet, server.URL+"/pair/"+id, "")
	page, err := io.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("pair page status = %d", resp.StatusCode)
	}
	if !strings.Contains(string(page), "Adicionar playlist ao Blazzing") {
		t.Fatal("pair page did not contain expected UI")
	}
	if got := resp.Header.Get("Cache-Control"); got != "no-store" {
		t.Fatalf("unexpected Cache-Control: %q", got)
	}
	if got := resp.Header.Get("Referrer-Policy"); got != "no-referrer" {
		t.Fatalf("unexpected Referrer-Policy: %q", got)
	}

	resp = request(t, client, http.MethodGet, server.URL+"/static/pair.js", "")
	js, err := io.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK || !strings.Contains(string(js), "AES-GCM") {
		t.Fatalf("pair JS missing crypto path, status=%d", resp.StatusCode)
	}

	resp = request(t, client, http.MethodGet, server.URL+"/api/v1/sessions/"+id+"/payload", "")
	if resp.StatusCode != http.StatusNoContent {
		t.Fatalf("empty poll status = %d", resp.StatusCode)
	}
	resp.Body.Close()

	payload := `{"iv":"abcdefghijklmnop","ciphertext":"abcdefghijklmnopqrstuvwxyz"}`
	resp = request(t, client, http.MethodPost, server.URL+"/api/v1/sessions/"+id+"/payload", payload)
	if resp.StatusCode != http.StatusNoContent {
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		t.Fatalf("submit status=%d body=%s", resp.StatusCode, string(body))
	}
	resp.Body.Close()

	resp = request(t, client, http.MethodGet, server.URL+"/api/v1/sessions/"+id+"/payload", "")
	gotPayload, err := io.ReadAll(resp.Body)
	resp.Body.Close()
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != http.StatusOK || !strings.Contains(string(gotPayload), "ciphertext") {
		t.Fatalf("poll status=%d body=%s", resp.StatusCode, string(gotPayload))
	}

	resp = request(t, client, http.MethodDelete, server.URL+"/api/v1/sessions/"+id, "")
	if resp.StatusCode != http.StatusNoContent {
		t.Fatalf("delete status = %d", resp.StatusCode)
	}
	resp.Body.Close()

	resp = request(t, client, http.MethodGet, server.URL+"/api/v1/sessions/"+id+"/payload", "")
	if resp.StatusCode != http.StatusGone {
		t.Fatalf("after delete status = %d", resp.StatusCode)
	}
	resp.Body.Close()
}

func TestForwardedIPTrustedOnlyFromLoopback(t *testing.T) {
	req := httptest.NewRequest(http.MethodPost, "http://relay/api/v1/sessions/x", nil)
	req.RemoteAddr = "203.0.113.50:40000"
	req.Header.Set("X-Real-IP", "198.51.100.10")
	if got := clientIP(req); got != "203.0.113.50" {
		t.Fatalf("untrusted proxy spoof accepted: %q", got)
	}

	req.RemoteAddr = "127.0.0.1:40000"
	if got := clientIP(req); got != "198.51.100.10" {
		t.Fatalf("trusted local proxy IP not used: %q", got)
	}
}
