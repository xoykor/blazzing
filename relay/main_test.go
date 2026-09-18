// SPDX-License-Identifier: MIT
package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestSessionLifecycle(t *testing.T) {
	app := newRelay()
	id := "0123456789abcdef0123456789abcdef"

	create := httptest.NewRequest(http.MethodPost, "/api/v1/sessions/"+id, nil)
	create.RemoteAddr = "127.0.0.1:12345"
	createOut := httptest.NewRecorder()
	app.api(createOut, create)
	if createOut.Code != http.StatusCreated {
		t.Fatalf("create status = %d", createOut.Code)
	}

	empty := httptest.NewRequest(http.MethodGet, "/api/v1/sessions/"+id+"/payload", nil)
	emptyOut := httptest.NewRecorder()
	app.api(emptyOut, empty)
	if emptyOut.Code != http.StatusNoContent {
		t.Fatalf("empty status = %d", emptyOut.Code)
	}

	payload := `{"iv":"abcdefghijklmnop","ciphertext":"abcdefghijklmnopqrstuvwxyz"}`
	submit := httptest.NewRequest(http.MethodPost, "/api/v1/sessions/"+id+"/payload", strings.NewReader(payload))
	submit.Header.Set("Content-Type", "application/json")
	submitOut := httptest.NewRecorder()
	app.api(submitOut, submit)
	if submitOut.Code != http.StatusNoContent {
		t.Fatalf("submit status = %d body=%s", submitOut.Code, submitOut.Body.String())
	}

	get := httptest.NewRequest(http.MethodGet, "/api/v1/sessions/"+id+"/payload", nil)
	getOut := httptest.NewRecorder()
	app.api(getOut, get)
	if getOut.Code != http.StatusOK || !strings.Contains(getOut.Body.String(), "ciphertext") {
		t.Fatalf("get status=%d body=%s", getOut.Code, getOut.Body.String())
	}

	del := httptest.NewRequest(http.MethodDelete, "/api/v1/sessions/"+id, nil)
	delOut := httptest.NewRecorder()
	app.api(delOut, del)
	if delOut.Code != http.StatusNoContent {
		t.Fatalf("delete status = %d", delOut.Code)
	}

	gone := httptest.NewRequest(http.MethodGet, "/api/v1/sessions/"+id+"/payload", nil)
	goneOut := httptest.NewRecorder()
	app.api(goneOut, gone)
	if goneOut.Code != http.StatusGone {
		t.Fatalf("gone status = %d", goneOut.Code)
	}
}

func TestRejectsInvalidSessionID(t *testing.T) {
	app := newRelay()
	req := httptest.NewRequest(http.MethodPost, "/api/v1/sessions/not-valid", nil)
	out := httptest.NewRecorder()
	app.api(out, req)
	if out.Code != http.StatusNotFound {
		t.Fatalf("status = %d", out.Code)
	}
}
