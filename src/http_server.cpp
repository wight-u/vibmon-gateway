#include "http_server.hpp"

#include <chrono>
#include <sstream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "protocol.hpp"

namespace vibmon {

using json = nlohmann::json;

static const char DASHBOARD_HTML[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>vibmon — live dashboard</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
  body{font-family:monospace;background:#111;color:#ccc;margin:0;padding:1rem}
  h1{color:#4af;margin:0 0 .5rem}
  .row{display:grid;gap:1rem;margin-top:1rem}
  .row-2{grid-template-columns:1fr 1fr}
  .row-3{grid-template-columns:1fr 1fr 1fr}
  canvas{background:#1a1a1a;border-radius:4px}
  h3{margin:.25rem 0}
  #status{font-size:.8rem;color:#888}
</style>
</head>
<body>
<h1>vibmon</h1>
<div id="status">connecting…</div>
<div class="row row-2">
  <div><h3 style="color:#4af">Accelerometer XYZ (g)</h3><canvas id="cXYZ"></canvas></div>
  <div></div>
</div>
<div class="row row-3">
  <div><h3 style="color:#f55">FFT — accel X</h3><canvas id="cFFTx"></canvas></div>
  <div><h3 style="color:#5f5">FFT — accel Y</h3><canvas id="cFFTy"></canvas></div>
  <div><h3 style="color:#55f">FFT — accel Z</h3><canvas id="cFFTz"></canvas></div>
</div>
<script>
const MAX_XYZ = 200;
const colors  = ['#f55','#5f5','#55f'];

function makeChart(id, datasets, xLabel, yLabel) {
  return new Chart(document.getElementById(id), {
    type:'line',
    data:{labels:[], datasets},
    options:{
      animation:false, responsive:true,
      plugins:{legend:{labels:{color:'#ccc'}}},
      scales:{
        x:{ticks:{color:'#888',maxTicksLimit:8},grid:{color:'#222'},title:{display:true,text:xLabel,color:'#888'}},
        y:{ticks:{color:'#888'},grid:{color:'#222'},title:{display:true,text:yLabel,color:'#888'}}
      }
    }
  });
}

const xyzBuf = {ax:[],ay:[],az:[]};
const chartXYZ = makeChart('cXYZ', [
  {label:'ax',data:xyzBuf.ax,borderColor:colors[0],borderWidth:1,pointRadius:0,tension:0},
  {label:'ay',data:xyzBuf.ay,borderColor:colors[1],borderWidth:1,pointRadius:0,tension:0},
  {label:'az',data:xyzBuf.az,borderColor:colors[2],borderWidth:1,pointRadius:0,tension:0},
], 'sample', 'g');

function makeFFTChart(id, color) {
  return makeChart(id, [
    {label:'|X(f)|',data:[],borderColor:color,borderWidth:1,pointRadius:0,tension:0,
     fill:true,backgroundColor:color+'28'}
  ], 'Hz', 'magnitude');
}

const chartFFTx = makeFFTChart('cFFTx', '#f55');
const chartFFTy = makeFFTChart('cFFTy', '#5f5');
const chartFFTz = makeFFTChart('cFFTz', '#55f');

const ws = new WebSocket(`ws://${location.hostname}:8081`);
ws.onopen  = () => document.getElementById('status').textContent = 'connected';
ws.onerror = () => document.getElementById('status').textContent = 'websocket error';
ws.onclose = () => document.getElementById('status').textContent = 'disconnected';

ws.onmessage = e => {
  const d = JSON.parse(e.data);

  xyzBuf.ax.push(d.xyz[0]); xyzBuf.ay.push(d.xyz[1]); xyzBuf.az.push(d.xyz[2]);
  if (xyzBuf.ax.length > MAX_XYZ) { xyzBuf.ax.shift(); xyzBuf.ay.shift(); xyzBuf.az.shift(); }
  chartXYZ.data.labels = Array.from({length:xyzBuf.ax.length},(_,i)=>i);
  chartXYZ.update('none');

  const freqLabels = d.freq.map(f=>f.toFixed(1));
  chartFFTx.data.labels = freqLabels; chartFFTx.data.datasets[0].data = d.mag_x; chartFFTx.update('none');
  chartFFTy.data.labels = freqLabels; chartFFTy.data.datasets[0].data = d.mag_y; chartFFTy.update('none');
  chartFFTz.data.labels = freqLabels; chartFFTz.data.datasets[0].data = d.mag_z; chartFFTz.update('none');
};
</script>
</body>
</html>
)html";

HttpServer::HttpServer(int port, FftWorker& fft, DbWriter& db, GatewayStats& stats)
    : port_(port), fft_(fft), db_(db), stats_(stats), svr_(std::make_unique<httplib::Server>()) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setup_routes() {
    // CORS for development convenience
    svr_->set_default_headers({{"Access-Control-Allow-Origin", "*"}});

    svr_->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(DASHBOARD_HTML, "text/html");
    });

    svr_->Get("/api/fft", [this](const httplib::Request&, httplib::Response& res) {
        const auto fft = fft_.latest();
        if (!fft) {
            res.status = 503;
            res.set_content("{}", "application/json");
            return;
        }

        json j;
        j["freq"]  = fft->freq;
        j["mag_x"] = fft->mag_x;
        j["mag_y"] = fft->mag_y;
        j["mag_z"] = fft->mag_z;
        j["ts"]    = fft->ts_ms;
        res.set_content(j.dump(), "application/json");
    });

    svr_->Get("/api/latest", [this](const httplib::Request& req, httplib::Response& res) {
        int limit = 100;
        if (req.has_param("limit"))
            limit = std::clamp(std::stoi(req.get_param_value("limit")), 1, 1000);

        std::vector<ImuSample> buf(limit);
        const int              n = db_.read_latest(buf.data(), limit);

        json arr = json::array();
        for (int i = 0; i < n; ++i) {
            const auto& s = buf[i];
            arr.push_back({{"ts", s.ts_ms},
                           {"ax", s.ax},
                           {"ay", s.ay},
                           {"az", s.az},
                           {"gx", s.gx},
                           {"gy", s.gy},
                           {"gz", s.gz}});
        }
        res.set_content(json{{"samples", arr}}.dump(), "application/json");
    });

    svr_->Get("/api/stats", [this](const httplib::Request&, httplib::Response& res) {
        using namespace std::chrono;
        const auto uptime_s =
            duration_cast<seconds>(steady_clock::now() - stats_.start_time).count();
        const uint64_t ok  = stats_.frames_ok.load();
        const double   fps = uptime_s > 0 ? static_cast<double>(ok) / uptime_s : 0.0;

        json j;
        j["uptime_s"]        = uptime_s;
        j["frame_rate"]      = fps;
        j["frames_received"] = ok;
        j["crc_errors"]      = stats_.frames_error.load();
        j["rows_written"]    = db_.rows_written();
        j["ws_clients"]      = stats_.ws_clients.load();
        res.set_content(j.dump(), "application/json");
    });
}

void HttpServer::start() {
    setup_routes();
    thread_ = std::thread([this] { svr_->listen("0.0.0.0", port_); });
}

void HttpServer::stop() {
    svr_->stop();
    if (thread_.joinable())
        thread_.join();
}

} // namespace vibmon
