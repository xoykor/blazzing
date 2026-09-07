#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import json, os, sys
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

ROOT = os.path.abspath(sys.argv[1])
PORT = int(sys.argv[2])

CATEGORIES = [
    {"category_id":"10","category_name":"Documentarios","parent_id":0},
    {"category_id":"20","category_name":"Noticias","parent_id":0},
    {"category_id":"30","category_name":"Esportes","parent_id":0},
]
VOD_CATEGORIES = [
    {"category_id":"110","category_name":"Filmes Ação","parent_id":0},
    {"category_id":"120","category_name":"Filmes Drama","parent_id":0},
]
SERIES_CATEGORIES = [
    {"category_id":"210","category_name":"Séries Ação","parent_id":0},
    {"category_id":"220","category_name":"Séries Drama","parent_id":0},
]

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass
    def send_bytes(self, data, ctype="application/octet-stream"):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        prefix = ""
        path = u.path
        for candidate in ("/alt", "/failstream"):
            if path.startswith(candidate + "/"):
                prefix = candidate
                path = path[len(candidate):]
                break
        if path == "/player_api.php":
            if q.get("username", [""])[0] != "test" or q.get("password", [""])[0] != "test":
                return self.send_bytes(json.dumps({"user_info":{"auth":0,"status":"Disabled"}}).encode(), "application/json")
            action = q.get("action", [""])[0]
            if not action:
                body = {"user_info":{"auth":1,"status":"Active"},"server_info":{"url":"127.0.0.1"}}
            elif action == "get_live_categories":
                body = CATEGORIES
            elif action == "get_live_streams":
                body = []
                for i in range(1, 13):
                    cat = "10" if i <= 4 else "20" if i <= 8 else "30"
                    body.append({
                        "num":i,"name":f"Canal Demo {i:02d}","stream_type":"live","stream_id":i,
                        "stream_icon":f"http://127.0.0.1:{PORT}/logo/{i}.jpg","epg_channel_id":"",
                        "added":"0","category_id":cat,"direct_source":""
                    })
            elif action == "get_vod_categories":
                body = VOD_CATEGORIES
            elif action == "get_vod_streams":
                body = [
                    {"name":f"Filme Demo {i:02d}","stream_type":"movie","stream_id":1000+i,
                     "stream_icon":f"http://127.0.0.1:{PORT}/logo/{i}.jpg",
                     "category_id":"110" if i <= 2 else "120","container_extension":"mp4","direct_source":""}
                    for i in range(1, 5)
                ]
            elif action == "get_series_categories":
                body = SERIES_CATEGORIES
            elif action == "get_series":
                body = [
                    {"series_id":2000+i,"name":f"Serie Demo {i:02d}",
                     "cover":f"http://127.0.0.1:{PORT}/logo/{i}.jpg",
                     "category_id":"210" if i <= 2 else "220"}
                    for i in range(1, 4)
                ]
            elif action == "get_series_info":
                sid = q.get("series_id", ["0"])[0]
                body = {
                    "info":{"name":f"Serie {sid}"},
                    "episodes":{
                        "1":[
                            {"id":3001,"episode_num":1,"title":"S01E01 - Piloto","container_extension":"mp4","info":{}},
                            {"id":3002,"episode_num":2,"title":"S01E02 - Continuação","container_extension":"mp4","info":{}},
                        ],
                        "2":[
                            {"id":3101,"episode_num":1,"title":"S02E01 - Retorno","container_extension":"mp4","info":{}}
                        ]
                    }
                }
            else:
                body = []
            return self.send_bytes(json.dumps(body).encode(), "application/json")
        if path.startswith("/live/test/test/") and path.endswith(".ts"):
            if prefix == "/failstream":
                self.send_response(503); self.end_headers(); return
            with open(os.path.join(ROOT,"stream.ts"),"rb") as f: return self.send_bytes(f.read(),"video/mp2t")
        if path.startswith("/movie/test/test/") or path.startswith("/series/test/test/"):
            with open(os.path.join(ROOT,"stream.ts"),"rb") as f: return self.send_bytes(f.read(),"video/mp2t")
        if path.startswith("/logo/") and path.endswith(".jpg"):
            idx = int(os.path.basename(path).split(".")[0])
            path = os.path.join(ROOT, f"logo{((idx-1)%4)+1}.jpg")
            with open(path,"rb") as f: return self.send_bytes(f.read(),"image/jpeg")
        self.send_response(404); self.end_headers()

ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
