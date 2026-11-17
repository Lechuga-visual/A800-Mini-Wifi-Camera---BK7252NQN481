"""Local UI preview. Simulated state only; never connects to or configures a camera."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler,HTTPServer
from urllib.parse import parse_qs,urlsplit
import argparse

root=Path(__file__).resolve().parents[1]
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        url=urlsplit(self.path)
        if url.path!='/':self.send_error(404,'Preview only; no real camera endpoints');return
        mode=parse_qs(url.query).get('state',['setup'])[0]
        if mode not in ['setup','configured','login']:mode='setup'
        html=(root/'firmware/index.html').read_text(encoding='utf-8')
        banner='<p style="padding:12px 0;color:#787774;font-size:12px">UI preview / Vista previa · Simulated state, no camera connection.</p>'
        mock='''<script>
let previewSignedIn=MODE!=='login',previewConfigured=MODE==='configured',previewWifi=false;
window.fetch=async(path,options={})=>{
 if(path==='/login')previewSignedIn=true;
 if(path==='/logout')previewSignedIn=false;
 if(path==='/passwords')previewConfigured=true;
 if(path==='/wifi')previewWifi=true;
 const status=previewSignedIn?200:401;
 return {ok:status===200,status,json:async()=>path==='/status'?{camera_ready:true,security_configured:previewConfigured,wifi_connected:previewWifi,wifi_configured:previewWifi,station_ip:previewWifi?'192.168.1.123':'',frames_sent:0}:{saved:true}};
};</script>'''.replace('MODE',repr(mode))
        html=html.replace('location.origin',"'https://192.168.4.1'").replace('<main>','<main>'+banner).replace('<script>const translations',mock+'<script>const translations')
        data=html.encode('utf-8');self.send_response(200)
        self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)))
        self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',type=int,default=8768)
    args=parser.parse_args();print(f'Preview: http://127.0.0.1:{args.port}/?state=setup',flush=True)
    HTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
