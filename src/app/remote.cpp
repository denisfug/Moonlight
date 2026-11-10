#include "app/remote.hpp"

#include <cstdio>
#include <cstring>

#include "core/json.hpp"
#include "net/tls.hpp"   // ml_platform_random

namespace ml {

namespace {
constexpr int kFirstPort = 8080;
constexpr size_t kMaxRequest = 32 * 1024;

// The phone page. Plain HTML/CSS/JS, no external files (the PS4 serves it, the phone may be offline).
const char kPage[] = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Moonlight HB</title>
<style>
:root{--bg:#0f0b1d;--card:#1b1533;--line:#2e2552;--text:#ece8ff;--dim:#a49cc8;--acc:#8b6cff;--acc2:#4f8cff;--bad:#ff6b8a}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:16px;max-width:520px;margin:auto}
h1{font-size:20px;margin:4px 0 12px;display:flex;align-items:center;gap:8px}
h1 span{width:10px;height:10px;border-radius:50%;background:var(--dim)}
h1 span.on{background:#4ade80}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
.lbl{color:var(--dim);font-size:13px;margin-bottom:6px}
input,textarea{width:100%;background:#120e24;color:var(--text);border:1px solid var(--line);border-radius:10px;padding:12px;font:inherit}
textarea{min-height:64px;resize:vertical}
button{background:var(--line);color:var(--text);border:0;border-radius:12px;padding:14px 0;font:inherit;font-weight:600;touch-action:manipulation}
button:active{background:var(--acc)}
.send{background:linear-gradient(135deg,var(--acc),var(--acc2));width:100%;margin-top:8px}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.row{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:8px}
.prompt{border-color:var(--acc)}
.msg{color:var(--dim);font-size:13px;min-height:18px;margin-top:6px}
.err{color:var(--bad)}
.hide{display:none}
</style></head><body>
<h1><span id="dot"></span>Moonlight HB</h1>
<div id="login" class="card">
 <div class="lbl">Digite o PIN que aparece na TV (Configurações &gt; Controle pelo celular)</div>
 <input id="pin" inputmode="numeric" maxlength="4" autocomplete="off">
 <button class="send" onclick="login()">Entrar</button>
 <div id="lmsg" class="msg"></div>
</div>
<div id="main" class="hide">
 <div id="ask" class="card prompt hide">
  <div class="lbl" id="askt">A TV está pedindo um texto</div>
  <textarea id="askv"></textarea>
  <button class="send" onclick="sendText(askv.value)">Enviar para a TV</button>
 </div>
 <div class="card">
  <div class="lbl" id="scr">Tela: -</div>
  <div class="pad">
   <button onclick="k('triangle')">&#9651;</button><button onclick="k('up')">&#9650;</button><button onclick="k('square')">&#9633;</button>
   <button onclick="k('left')">&#9664;</button><button onclick="k('cross')">X</button><button onclick="k('right')">&#9654;</button>
   <button onclick="k('circle')">O Voltar</button><button onclick="k('down')">&#9660;</button><button onclick="k('options')">Options</button>
  </div>
  <div class="row"><button onclick="k('l2')">L2</button><button onclick="k('l1')">L1</button><button onclick="k('r1')">R1</button><button onclick="k('r2')">R2</button></div>
 </div>
 <div class="card">
  <div class="lbl">Adicionar fonte (endereço que termina em manifest.json)</div>
  <textarea id="src" placeholder="https://.../manifest.json"></textarea>
  <button class="send" onclick="addSource()">Adicionar fonte</button>
 </div>
 <div id="msg" class="msg"></div>
</div>
<script>
let PIN='';const $=id=>document.getElementById(id);let lastPrompt='';
function say(t,bad){$('msg').textContent=t;$('msg').className='msg'+(bad?' err':'')}
async function call(path,body){
 const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:{'X-Pin':PIN},body:body});
 if(r.status==403){$('main').className='hide';$('login').className='card';$('lmsg').textContent='PIN incorreto ou bloqueado por 1 minuto';$('lmsg').className='msg err';throw 0}
 return r.text()}
async function login(){PIN=$('pin').value.trim();try{await call('/api/state');$('login').className='hide';$('main').className='';poll()}catch(e){}}
async function k(b){try{await call('/api/key',b)}catch(e){}}
async function sendText(t){try{await call('/api/text',t);say('Texto enviado para a TV.');$('askv').value=''}catch(e){}}
async function addSource(){const u=$('src').value.trim();if(!u)return;try{await call('/api/source',u);say('Pedido enviado. Veja o resultado na TV.');$('src').value=''}catch(e){}}
async function poll(){
 try{const s=JSON.parse(await call('/api/state'));$('dot').className='on';$('scr').textContent='Tela: '+(s.screen||'-');
  if(s.prompt){$('ask').className='card prompt';$('askt').textContent=s.title||'A TV está pedindo um texto';
   if(lastPrompt!==s.title){$('askv').value=s.initial||'';lastPrompt=s.title}}
  else{$('ask').className='card prompt hide';lastPrompt=''}
 }catch(e){$('dot').className=''}
 setTimeout(poll,1500)}
$('pin').addEventListener('keydown',e=>{if(e.key==='Enter')login()});
</script></body></html>)HTML";

std::string response(int code, const char *reason, const std::string &type, const std::string &body)
{
    char head[256];
    std::snprintf(head, sizeof head,
                  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                  "Connection: close\r\n\r\n",
                  code, reason, type.c_str(), body.size());
    return head + body;
}

uint32_t buttonFromName(const std::string &n)
{
    if (n == "up") return BtnUp;
    if (n == "down") return BtnDown;
    if (n == "left") return BtnLeft;
    if (n == "right") return BtnRight;
    if (n == "cross") return BtnCross;
    if (n == "circle") return BtnCircle;
    if (n == "square") return BtnSquare;
    if (n == "triangle") return BtnTriangle;
    if (n == "l1") return BtnL1;
    if (n == "r1") return BtnR1;
    if (n == "l2") return BtnL2;
    if (n == "r2") return BtnR2;
    if (n == "options") return BtnOptions;
    return 0;
}
}  // namespace

RemoteControl::~RemoteControl() { stop(); }

bool RemoteControl::start(std::string *err)
{
    if (running_) return true;
    Sockets *s = platform_.sockets();
    if (!s) { if (err) *err = "rede indisponivel neste aparelho"; return false; }
    int port = kFirstPort;
    std::string e;
    for (; port < kFirstPort + 10 && !listener_; ++port) listener_ = s->listen(port, &e);
    if (!listener_) { if (err) *err = "nao abriu a porta: " + e; return false; }
    --port;
    unsigned char r[2] = {0, 0};
    if (ml_platform_random(r, sizeof r) != 0) { listener_.reset(); if (err) *err = "gerador aleatorio falhou"; return false; }
    char pin[8];
    std::snprintf(pin, sizeof pin, "%04u", unsigned((r[0] << 8) | r[1]) % 10000u);
    pin_ = pin;
    std::string ip = s->localAddress();
    address_ = "http://" + (ip.empty() ? std::string("<IP do PS4>") : ip) + ":" + std::to_string(port);
    failures_ = 0;
    lockedUntil_ = 0;
    quit_ = false;
    running_ = true;
    if (!platform_.startThread(&RemoteControl::threadMain, this, "ml_remote")) {
        running_ = false;
        listener_.reset();
        if (err) *err = "nao iniciou a thread";
        return false;
    }
    return true;
}

void RemoteControl::stop()
{
    if (!running_) return;
    quit_ = true;
    for (int i = 0; i < 300 && running_.load(); i++) platform_.sleepMs(10);   // accept() wakes every 250 ms
    listener_.reset();
}

void RemoteControl::threadMain(void *self) { static_cast<RemoteControl *>(self)->loop(); }

void RemoteControl::loop()
{
    while (!quit_) {
        std::string peer;
        std::unique_ptr<TcpConn> c = listener_->accept(250, &peer);
        if (!c) continue;
        std::string req;
        char buf[4096];
        size_t need = std::string::npos;   // total bytes once the header is parsed
        while (req.size() < kMaxRequest) {
            long n = c->recv(buf, sizeof buf);
            if (n <= 0) break;
            req.append(buf, size_t(n));
            size_t he = req.find("\r\n\r\n");
            if (he == std::string::npos) continue;
            if (need == std::string::npos) {
                need = he + 4;
                std::string low = req.substr(0, he);
                for (auto &ch : low) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
                size_t cl = low.find("\r\ncontent-length:");
                if (cl != std::string::npos) need += size_t(std::strtoul(low.c_str() + cl + 17, nullptr, 10));
            }
            if (req.size() >= need) break;
        }
        std::string out = handle(req, platform_.nowMs());
        size_t done = 0;
        while (done < out.size()) {
            long n = c->send(out.data() + done, out.size() - done);
            if (n <= 0) break;
            done += size_t(n);
        }
    }
    running_ = false;
}

std::string RemoteControl::handle(const std::string &req, uint64_t now)
{
    size_t he = req.find("\r\n\r\n");
    if (he == std::string::npos || req.size() > kMaxRequest) return response(400, "Bad Request", "text/plain", "pedido invalido");
    std::string head = req.substr(0, he), body = req.substr(he + 4);
    size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return response(400, "Bad Request", "text/plain", "pedido invalido");
    std::string method = head.substr(0, sp1), path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    path = path.substr(0, path.find('?'));

    if (method == "GET" && (path == "/" || path == "/index.html"))
        return response(200, "OK", "text/html; charset=utf-8", std::string(kPage, sizeof kPage - 1));
    if (path.compare(0, 5, "/api/") != 0) return response(404, "Not Found", "text/plain", "nao encontrado");

    // PIN check with a lockout against guessing (10 000 combinations).
    std::string pin;
    std::string low = head;
    for (auto &ch : low) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    size_t ph = low.find("\r\nx-pin:");
    if (ph != std::string::npos) {
        size_t a = ph + 8, e = head.find("\r\n", a);
        pin = head.substr(a, e == std::string::npos ? std::string::npos : e - a);
        while (!pin.empty() && pin.front() == ' ') pin.erase(0, 1);
        while (!pin.empty() && pin.back() == ' ') pin.pop_back();
    }
    lock();
    bool locked = now < lockedUntil_;
    bool ok = !locked && !pin_.empty() && pin == pin_;
    if (!ok && !locked && ++failures_ >= 5) { lockedUntil_ = now + 60000; failures_ = 0; }
    if (ok) failures_ = 0;
    std::string state = state_;
    unlock();
    if (!ok) return response(403, "Forbidden", "text/plain", locked ? "bloqueado por 1 minuto" : "PIN incorreto");
    lastSeen_ = now;

    if (method == "GET" && path == "/api/state") return response(200, "OK", "application/json", state);
    if (method != "POST") return response(405, "Method Not Allowed", "text/plain", "use POST");
    Command cmd{Command::Buttons, 0, ""};
    if (path == "/api/key") {
        cmd.buttons = buttonFromName(body);
        if (!cmd.buttons) return response(400, "Bad Request", "text/plain", "botao desconhecido");
    } else if (path == "/api/text") {
        cmd.kind = Command::Text;
        cmd.text = body.substr(0, 2048);
    } else if (path == "/api/source") {
        cmd.kind = Command::Source;
        cmd.text = body.substr(0, 2048);
        while (!cmd.text.empty() && (cmd.text.back() == '\n' || cmd.text.back() == '\r' || cmd.text.back() == ' ')) cmd.text.pop_back();
    } else {
        return response(404, "Not Found", "text/plain", "nao encontrado");
    }
    lock();
    if (queue_.size() < 64) queue_.push_back(cmd);
    unlock();
    return response(200, "OK", "text/plain", "ok");
}

std::vector<RemoteControl::Command> RemoteControl::take()
{
    std::vector<Command> out;
    lock();
    out.swap(queue_);
    unlock();
    return out;
}

void RemoteControl::setState(const std::string &screen, bool prompt, const std::string &title, const std::string &initial, bool url)
{
    Json j = Json::object();
    j.set("screen", Json::string(screen));
    j.set("prompt", Json::boolean(prompt));
    if (prompt) {
        j.set("title", Json::string(title));
        j.set("initial", Json::string(initial));
        j.set("url", Json::boolean(url));
    }
    std::string s = j.dump();
    lock();
    state_.swap(s);
    unlock();
}

}  // namespace ml
