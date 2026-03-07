#include "dashboard_writer.h"

#include <fstream>
#include <sstream>
#include <utility>

DashboardWriter::DashboardWriter(std::string outputPath) : outputPath_(std::move(outputPath)) {}

std::string DashboardWriter::Escape(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

void DashboardWriter::Write(const TelemetrySnapshot& t,
                            const std::vector<AiAction>& proposed,
                            const std::vector<ActionResult>& executed,
                            const std::vector<LearningStat>& learning,
                            const std::vector<TelemetrySnapshot>& recent) const {
    std::ofstream out(outputPath_, std::ios::trunc);
    if (!out.is_open()) return;

    std::ostringstream severityData;
    for (size_t i = 0; i < recent.size(); ++i) {
        if (i) severityData << ',';
        severityData << recent[i].inferredSeverity;
    }

    const std::string fpsText = (t.observedFps >= 0.0) ? std::to_string(t.observedFps) : "N/A";

    out << "<!doctype html><html><head><meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        << "<meta http-equiv='refresh' content='1'>"
        << "<title>God of Frames Dashboard</title>"
        << "<style>"
        << ":root{--bg:#0b1020;--card:#111a33;--line:#22345f;--text:#e9eefc;--muted:#95a6ce;--ok:#2ec27e;--warn:#f6c64d;--bad:#ff6b6b;--accent:#3aa3ff;}"
        << "*{box-sizing:border-box}body{margin:0;font-family:Segoe UI,system-ui,sans-serif;background:radial-gradient(1200px 700px at 15% -20%,#1e2a50 0%,var(--bg) 60%);color:var(--text)}"
        << ".wrap{max-width:1100px;margin:28px auto;padding:0 16px}.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}"
        << ".title{font-size:26px;font-weight:700;letter-spacing:.2px}.sub{color:var(--muted)}"
        << ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin:16px 0}"
        << ".card{background:linear-gradient(180deg,#152348 0%,var(--card) 100%);border:1px solid var(--line);border-radius:14px;padding:14px}"
        << ".k{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}.v{font-size:28px;font-weight:700;margin-top:4px}"
        << ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px}.panel{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-top:12px}"
        << "ul{margin:8px 0 0;padding:0;list-style:none}li{padding:8px 10px;border-radius:10px;margin-bottom:8px;background:#0e1832;border:1px solid #1b2d58}"
        << ".ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}canvas{width:100%;height:170px;background:#0d1630;border-radius:10px;border:1px solid #1d2f5d}"
        << "@media (max-width:850px){.row{grid-template-columns:1fr}}"
        << "</style></head><body><div class='wrap'>"
        << "<div class='top'><div><div class='title'>God of Frames</div><div class='sub'>Adaptive optimizer for " << Escape(t.gameExe) << "</div></div>"
        << "<div class='sub'>Auto-refresh every 1s</div></div>"
        << "<div class='grid'>"
        << "<div class='card'><div class='k'>Severity</div><div class='v'>" << t.inferredSeverity << "</div></div>"
        << "<div class='card'><div class='k'>FPS</div><div class='v'>" << fpsText << "</div></div>"
        << "<div class='card'><div class='k'>CPU %</div><div class='v'>" << t.processCpuPercent << "</div></div>"
        << "<div class='card'><div class='k'>System Memory %</div><div class='v'>" << t.systemMemoryUsedPercent << "</div></div>"
        << "<div class='card'><div class='k'>Game RAM MB</div><div class='v'>" << t.processWorkingSetMB << "</div></div>"
        << "</div>"
        << "<div class='panel'><div class='k'>Severity Trend</div><canvas id='c' width='1000' height='220'></canvas></div>"
        << "<div class='row'>"
        << "<div class='panel'><div class='k'>Proposed Actions</div><ul>";

    if (proposed.empty()) {
        out << "<li class='warn'>No action proposed this cycle.</li>";
    } else {
        for (const auto& a : proposed) {
            out << "<li><strong>" << Escape(a.token) << "</strong> <span class='sub'>" << Escape(a.details) << " (" << Escape(a.source) << ")</span></li>";
        }
    }

    out << "</ul></div><div class='panel'><div class='k'>Executed / Notifications</div><ul>";

    if (executed.empty()) {
        out << "<li class='warn'>No execution events.</li>";
    } else {
        for (const auto& r : executed) {
            const char* cls = r.success ? "ok" : (r.kind == ActionKind::Notify ? "warn" : "bad");
            out << "<li class='" << cls << "'><strong>" << Escape(r.token) << "</strong> - " << Escape(r.message) << "</li>";
        }
    }

    out << "</ul></div></div>"
        << "<div class='panel'><div class='k'>Learning Scores (higher = more effective)</div><ul>";

    if (learning.empty()) {
        out << "<li class='warn'>No learned action history yet.</li>";
    } else {
        for (const auto& s : learning) {
            out << "<li><strong>" << Escape(s.token) << "</strong> score=" << s.score << " attempts=" << s.attempts << " success=" << s.successes << "</li>";
        }
    }

    out << "</ul></div>"
        << "</div><script>"
        << "const points=[" << severityData.str() << "];"
        << "const c=document.getElementById('c');const g=c.getContext('2d');"
        << "g.fillStyle='#0d1630';g.fillRect(0,0,c.width,c.height);g.strokeStyle='#3aa3ff';g.lineWidth=3;"
        << "if(points.length>1){g.beginPath();for(let i=0;i<points.length;i++){const x=i*(c.width/(points.length-1));const y=(1-Math.max(0,Math.min(1,points[i])))*(c.height-20)+10;if(i===0)g.moveTo(x,y);else g.lineTo(x,y);}g.stroke();}"
        << "g.strokeStyle='#22345f';g.lineWidth=1;for(let i=0;i<=5;i++){const y=10+i*(c.height-20)/5;g.beginPath();g.moveTo(0,y);g.lineTo(c.width,y);g.stroke();}"
        << "</script></body></html>";
}


