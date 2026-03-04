#include "MainFrame.h"

#include "App.h"
#include <iostream>

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/datetime.h>
#include <wx/graphics.h>
#include <wx/dcbuffer.h>
#include <wx/config.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

static int ffmpegInterruptCb(void* opaque) {
    // Return 1 to abort any blocking FFmpeg call when m_running is false
    return static_cast<std::atomic<bool>*>(opaque)->load() ? 0 : 1;
}

// ─────────────────────────────────────────────
//  VideoPanel
// ─────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(VideoPanel, wxPanel)
    EVT_PAINT(VideoPanel::OnPaint)
    EVT_SIZE(VideoPanel::OnSize)
wxEND_EVENT_TABLE()

VideoPanel::VideoPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxBLACK);
}

void VideoPanel::SetFrame(const wxImage& img) {
    m_image = img;
    // Scale to panel size, maintaining aspect ratio
    wxSize sz = GetClientSize();
    if (sz.x > 0 && sz.y > 0 && m_image.IsOk()) {
        double scaleX = (double)sz.x / m_image.GetWidth();
        double scaleY = (double)sz.y / m_image.GetHeight();
        double scale  = std::min(scaleX, scaleY);
        int newW = (int)(m_image.GetWidth()  * scale);
        int newH = (int)(m_image.GetHeight() * scale);
        m_bitmap = wxBitmap(m_image.Scale(newW, newH, wxIMAGE_QUALITY_BILINEAR));
    }
    Refresh(false);
}

void VideoPanel::Clear() {
    m_image  = wxImage();
    m_bitmap = wxBitmap();
    Refresh(false);
}

void VideoPanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(*wxBLACK_BRUSH);
    dc.Clear();

    if (!m_bitmap.IsOk()) {
        // Draw placeholder text
        dc.SetTextForeground(wxColour(80, 80, 80));
        dc.SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                          wxFONTWEIGHT_NORMAL));
        wxSize sz = GetClientSize();
        wxString msg("No stream connected");
        wxSize tsz = dc.GetTextExtent(msg);
        dc.DrawText(msg, (sz.x - tsz.x) / 2, (sz.y - tsz.y) / 2);
        return;
    }

    wxSize sz  = GetClientSize();
    wxSize bsz = m_bitmap.GetSize();
    int x = (sz.x - bsz.x) / 2;
    int y = (sz.y - bsz.y) / 2;
    dc.DrawBitmap(m_bitmap, x, y, false);
}

void VideoPanel::OnSize(wxSizeEvent& evt) {
    // Re-scale cached bitmap on resize
    if (m_image.IsOk()) {
        wxSize sz = evt.GetSize();
        if (sz.x > 0 && sz.y > 0) {
            double scaleX = (double)sz.x / m_image.GetWidth();
            double scaleY = (double)sz.y / m_image.GetHeight();
            double scale  = std::min(scaleX, scaleY);
            int newW = (int)(m_image.GetWidth()  * scale);
            int newH = (int)(m_image.GetHeight() * scale);
            m_bitmap = wxBitmap(m_image.Scale(newW, newH, wxIMAGE_QUALITY_BILINEAR));
        }
    }
    Refresh(false);
    evt.Skip();
}

// ─────────────────────────────────────────────
//  MainFrame – event table
// ─────────────────────────────────────────────
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(ID_CONNECT,    MainFrame::OnConnect)
    EVT_BUTTON(ID_DISCONNECT, MainFrame::OnDisconnect)
    EVT_BUTTON(ID_SNAPSHOT,   MainFrame::OnSnapshot)
    EVT_BUTTON(ID_LIGHT,      MainFrame::OnLight)
    EVT_TIMER(ID_FRAME_TIMER, MainFrame::OnFrameTimer)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

// ─────────────────────────────────────────────
//  MainFrame – construction
// ─────────────────────────────────────────────
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Flashforge 3D Printer Camera Viewer",
              wxDefaultPosition, wxSize(900, 620)),
      m_frameTimer(this, ID_FRAME_TIMER)
{
    BuildUI();

    // Minimum size: wide enough for the full toolbar without clipping,
    // tall enough for toolbar (44) + a usable video area (240) + status bar (24)
    SetMinSize(wxSize(660, 308));

    Centre();
}

MainFrame::~MainFrame() {
    StopStream();
}

void MainFrame::BuildUI() {
    SetBackgroundColour(wxColour(30, 30, 30));

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    // ── Top toolbar ──────────────────────────────────
    wxPanel* toolbar = new wxPanel(this, wxID_ANY);
    toolbar->SetBackgroundColour(wxColour(45, 45, 45));
    wxBoxSizer* tbSizer = new wxBoxSizer(wxHORIZONTAL);

    auto label = [&](const wxString& text) {
        auto* l = new wxStaticText(toolbar, wxID_ANY, text);
        l->SetForegroundColour(wxColour(180, 180, 180));
        return l;
    };

    tbSizer->AddSpacer(8);
    tbSizer->Add(label("URL:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    m_urlCtrl = new wxTextCtrl(toolbar, wxID_ANY, "",
                               wxDefaultPosition, wxSize(340, -1));
    tbSizer->Add(m_urlCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    // Buttons
    m_connectBtn = new wxButton(toolbar, ID_CONNECT, "Connect",
                                wxDefaultPosition, wxSize(90, 28));
    m_connectBtn->SetBackgroundColour(wxColour(0, 120, 60));
    m_connectBtn->SetForegroundColour(*wxWHITE);
    tbSizer->Add(m_connectBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_disconnectBtn = new wxButton(toolbar, ID_DISCONNECT, "Disconnect",
                                   wxDefaultPosition, wxSize(90, 28));
    m_disconnectBtn->SetBackgroundColour(wxColour(140, 30, 30));
    m_disconnectBtn->SetForegroundColour(*wxWHITE);
    m_disconnectBtn->Enable(false);
    tbSizer->Add(m_disconnectBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_snapshotBtn = new wxButton(toolbar, ID_SNAPSHOT, "Snapshot",
                                 wxDefaultPosition, wxSize(90, 28));
    m_snapshotBtn->SetBackgroundColour(wxColour(40, 80, 160));
    m_snapshotBtn->SetForegroundColour(*wxWHITE);
    m_snapshotBtn->Enable(false);
    tbSizer->Add(m_snapshotBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    m_lightBtn = new wxButton(toolbar, ID_LIGHT, "Light",
                              wxDefaultPosition, wxSize(70, 28));
    m_lightBtn->SetBackgroundColour(wxColour(60, 60, 60));
    m_lightBtn->SetForegroundColour(*wxWHITE);
    tbSizer->Add(m_lightBtn, 0, wxALIGN_CENTER_VERTICAL);

    tbSizer->AddStretchSpacer();

    m_fpsLabel = new wxStaticText(toolbar, wxID_ANY, "");
    m_fpsLabel->SetForegroundColour(wxColour(100, 200, 100));
    m_fpsLabel->SetMinSize(wxSize(m_fpsLabel->GetTextExtent("00.0 fps").x, -1));
    tbSizer->Add(m_fpsLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    toolbar->SetSizer(tbSizer);
    toolbar->SetMinSize(wxSize(-1, 44));
    root->Add(toolbar, 0, wxEXPAND);

    // ── Video panel ──────────────────────────────────
    m_videoPanel = new VideoPanel(this);
    m_videoPanel->SetMinSize(wxSize(640, 480));
    root->Add(m_videoPanel, 1, wxEXPAND | wxALL, 4);

    // ── Status bar ───────────────────────────────────
    wxPanel* statusBar = new wxPanel(this, wxID_ANY);
    statusBar->SetBackgroundColour(wxColour(25, 25, 25));
    wxBoxSizer* sbSizer = new wxBoxSizer(wxHORIZONTAL);

    m_statusLabel = new wxStaticText(statusBar, wxID_ANY,
                                     "Ready. Enter printer IP and click Connect.");
    m_statusLabel->SetForegroundColour(wxColour(160, 160, 160));
    sbSizer->AddSpacer(8);
    sbSizer->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL);
    statusBar->SetSizer(sbSizer);
    statusBar->SetMinSize(wxSize(-1, 24));
    root->Add(statusBar, 0, wxEXPAND);

    SetSizer(root);

    // ── Restore persisted settings ───────────────
    LoadSettings();
}

void MainFrame::LoadSettings() {
    wxConfig cfg("FlashView");
    wxString url;
    if (cfg.Read("url", &url)) {
        m_urlCtrl->SetValue(url);
    } else {
        // Migrate from old ip+port+path format
        wxString ip = cfg.Read("ip", "192.168.1.100");
        // Strip full URL that old builds stored in the ip field
        if (ip.StartsWith("rtsp://") || ip.StartsWith("http://")) {
            m_urlCtrl->SetValue(ip);
        } else {
            long port = cfg.ReadLong("port", 8080);
            wxString path = cfg.Read("path", "/?action=stream");
            m_urlCtrl->SetValue(wxString::Format("http://%s:%ld%s", ip, port, path));
        }
    }
}

void MainFrame::SaveSettings() {
    wxConfig cfg("FlashView");
    cfg.Write("url", m_urlCtrl->GetValue());
    cfg.Flush();
}

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
void MainFrame::UpdateStatus(const wxString& msg, bool error) {
    wxColour col = error ? wxColour(220, 80, 80) : wxColour(160, 160, 160);
    m_statusLabel->SetForegroundColour(col);
    m_statusLabel->SetLabel(msg);
}

void MainFrame::SetConnectedState(bool connected) {
    m_connectBtn->Enable(!connected);
    m_disconnectBtn->Enable(connected);
    m_snapshotBtn->Enable(connected);
    m_urlCtrl->Enable(!connected);
}

// ─────────────────────────────────────────────
//  Event handlers
// ─────────────────────────────────────────────
void MainFrame::OnConnect(wxCommandEvent&) {
    if (m_running) return;

    if (m_streamThread.joinable())
        m_streamThread.join();

    wxString url = m_urlCtrl->GetValue().Trim();
    if (url.IsEmpty()) {
        UpdateStatus("Please enter a stream URL.", true);
        return;
    }

    UpdateStatus(wxString::Format("Connecting to %s ...", url));
    SetConnectedState(true);

    m_running = true;
    m_frameCount = 0;
    m_fpsLastTime = wxGetUTCTimeMillis().GetValue();
    m_fpsLabel->SetLabel("");

    SaveSettings();
    m_streamUrl = url.ToStdString();

    m_streamThread = std::thread(&MainFrame::StreamThread, this);
    m_frameTimer.Start(30); // ~33 fps UI poll
}

void MainFrame::OnDisconnect(wxCommandEvent&) {
    StopStream();
    m_videoPanel->Clear();
    SetConnectedState(false);
    m_fpsLabel->SetLabel("");
    UpdateStatus("Disconnected.");
}

void MainFrame::OnSnapshot(wxCommandEvent&) {
    wxImage snapshot;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_pendingFrame.IsOk()) {
            UpdateStatus("No frame available for snapshot.", true);
            return;
        }
        snapshot = m_pendingFrame;
    }

    wxString dir = wxStandardPaths::Get().GetUserDataDir();
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxDateTime now = wxDateTime::Now();
    wxString filename = wxString::Format("snapshot_%s.png",
        now.Format("%Y%m%d_%H%M%S"));
    wxString fullPath = wxFileName(dir, filename).GetFullPath();

    if (snapshot.SaveFile(fullPath, wxBITMAP_TYPE_PNG)) {
        UpdateStatus(wxString::Format("Snapshot saved: %s", fullPath));
    } else {
        UpdateStatus("Failed to save snapshot.", true);
    }
}

// ─────────────────────────────────────────────
//  Light control helpers
// ─────────────────────────────────────────────
static std::string ExtractHost(const std::string& url) {
    std::string s = url;
    auto schemeEnd = s.find("://");
    if (schemeEnd != std::string::npos) s = s.substr(schemeEnd + 3);
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    auto colon = s.find(':');
    if (colon != std::string::npos) s = s.substr(0, colon);
    return s;
}

// Returns empty string on success, error description on failure.
static std::string SendLightCommand(const std::string& host, bool on) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "socket() failed";

    struct timeval tv{5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(8899);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return "Invalid IP: " + host;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return "Cannot connect to " + host + ":8899";
    }

    // Send each command then read the printer's acknowledgement before proceeding.
    // Sending all commands in one write causes the printer to ignore later ones.
    char buf[512];
    auto exchange = [&](const char* cmd) {
        send(sock, cmd, strlen(cmd), 0);
        memset(buf, 0, sizeof(buf));
        recv(sock, buf, sizeof(buf) - 1, 0);
    };

    exchange("~M601 S1\r\n");
    exchange(on ? "~M146 r255 g255 b255 F0\r\n" : "~M146 r0 g0 b0 F0\r\n");
    exchange("~M602\r\n");

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return "";
}

void MainFrame::OnLight(wxCommandEvent&) {
    std::string host = ExtractHost(m_urlCtrl->GetValue().ToStdString());
    if (host.empty()) {
        UpdateStatus("Cannot determine printer IP from URL.", true);
        return;
    }

    m_lightOn = !m_lightOn;
    bool on = m_lightOn;

    m_lightBtn->SetBackgroundColour(on ? wxColour(200, 160, 30) : wxColour(60, 60, 60));
    m_lightBtn->Refresh();
    m_lightBtn->Enable(false);
    UpdateStatus(wxString::Format("Turning light %s ...", on ? "on" : "off"));

    std::thread([this, host, on]() {
        std::string err = SendLightCommand(host, on);
        wxGetApp().CallAfter([this, on, err]() {
            m_lightBtn->Enable(true);
            if (err.empty()) {
                UpdateStatus(wxString::Format("Light %s.", on ? "on" : "off"));
            } else {
                // Revert optimistic toggle
                m_lightOn = !on;
                m_lightBtn->SetBackgroundColour(m_lightOn ? wxColour(200, 160, 30) : wxColour(60, 60, 60));
                m_lightBtn->Refresh();
                UpdateStatus(wxString::Format("Light error: %s", err), true);
            }
        });
    }).detach();
}

void MainFrame::OnFrameTimer(wxTimerEvent&) {
    // Always check: join finished thread and clean up, even if no frames arrived
    if (!m_running && m_streamThread.joinable()) {
        m_streamThread.join();
        m_frameTimer.Stop();
        SetConnectedState(false);
        m_fpsLabel->SetLabel("");
        return;
    }

    if (!m_hasNewFrame.exchange(false)) return;

    wxImage frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        frame = m_pendingFrame;
    }

    if (frame.IsOk()) {
        m_videoPanel->SetFrame(frame);

        // FPS counter
        m_frameCount++;
        long long now  = wxGetUTCTimeMillis().GetValue();
        long long diff = now - m_fpsLastTime;
        if (diff >= 1000) {
            m_currentFps = m_frameCount * 1000.0 / diff;
            m_fpsLabel->SetLabel(wxString::Format("%.1f fps", m_currentFps));
            m_frameCount  = 0;
            m_fpsLastTime = now;
        }
    }
}

void MainFrame::OnClose(wxCloseEvent& evt) {
    StopStream();
    SaveSettings();
    evt.Skip();
}

// ─────────────────────────────────────────────
//  StopStream
// ─────────────────────────────────────────────
void MainFrame::StopStream() {
    m_running = false;
    m_frameTimer.Stop();
    if (m_streamThread.joinable())
        m_streamThread.join();
}

// ─────────────────────────────────────────────
//  StreamThread  (runs on a background thread)
// ─────────────────────────────────────────────
void MainFrame::StreamThread() {
    const std::string urlStr = m_streamUrl; // written by main thread before start

    // ── Open input ───────────────────────────────
    // Allocate context first so we can install an interrupt callback that lets
    // StopStream() unblock any hanging av_read_frame / avformat_open_input call.
    AVFormatContext* fmtCtx = avformat_alloc_context();
    fmtCtx->interrupt_callback.callback = ffmpegInterruptCb;
    fmtCtx->interrupt_callback.opaque   = &m_running;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout",       "5000000", 0); // 5 s connect timeout
    av_dict_set(&opts, "analyzeduration","500000",  0);
    av_dict_set(&opts, "probesize",      "500000",  0);

    int ret = avformat_open_input(&fmtCtx, urlStr.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
#ifndef NDEBUG
        std::cerr << "[FlashView] Failed to open stream: " << errbuf << std::endl;
#endif
        wxString msg = wxString::Format("Failed to open stream: %s", errbuf);
        wxGetApp().CallAfter([this, msg]() {
            UpdateStatus(msg, true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("Could not find stream info.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    // ── Find video stream ────────────────────────
    int videoStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = (int)i;
            break;
        }
    }

    if (videoStreamIdx < 0) {
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("No video stream found in RTSP feed.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    // ── Open codec ───────────────────────────────
    AVCodecParameters* par   = fmtCtx->streams[videoStreamIdx]->codecpar;
    const AVCodec*     codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("Unsupported video codec.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("Could not allocate codec context.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    if (avcodec_parameters_to_context(codecCtx, par) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("Could not copy codec parameters.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }
    codecCtx->thread_count = 0; // auto

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        wxGetApp().CallAfter([this]() {
            UpdateStatus("Could not open codec.", true);
            SetConnectedState(false);
        });
        m_running = false;
        return;
    }

    wxGetApp().CallAfter([this, urlStr]() {
        UpdateStatus(wxString::Format("Connected to %s", urlStr));
    });
#ifndef NDEBUG
    std::cout << "[FlashView] Connected to: " << urlStr << std::endl;
#endif

    // ── Decode loop ──────────────────────────────
    AVPacket* packet  = av_packet_alloc();
    AVFrame*  frame   = av_frame_alloc();
    SwsContext* swsCtx = nullptr;

    int bufSize = 0;
    std::vector<uint8_t> buffer;

    int decodedCount = 0;
    while (m_running) {
        ret = av_read_frame(fmtCtx, packet);
        if (ret == AVERROR(EAGAIN)) { continue; }
        if (ret < 0) break;  // EOF, error, or interrupted

        if (packet->stream_index != videoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) continue; // skip undecodable packet

        while (avcodec_receive_frame(codecCtx, frame) == 0) {
            int w = frame->width;
            int h = frame->height;

            // Lazy-init sws
            swsCtx = sws_getCachedContext(swsCtx,
                w, h, (AVPixelFormat)frame->format,
                w, h, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            int newBufSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
            if (newBufSize != bufSize) {
                buffer.resize(newBufSize);
                bufSize = newBufSize;
            }

            uint8_t* data[1]     = { buffer.data() };
            int      linesize[1] = { 3 * w };
            sws_scale(swsCtx,
                      (const uint8_t* const*)frame->data, frame->linesize,
                      0, h, data, linesize);

            // Build wxImage from RGB buffer (copy data)
            unsigned char* rgbCopy = (unsigned char*)malloc(newBufSize);
            memcpy(rgbCopy, buffer.data(), newBufSize);
            wxImage img(w, h, rgbCopy, false); // takes ownership

            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_pendingFrame = img;
            }
            m_hasNewFrame = true;
            decodedCount++;
#ifndef NDEBUG
            if ((decodedCount % 100) == 0)
                std::cout << "[FlashView] Decoded " << decodedCount << " frames" << std::endl;
#endif
        }
    }

    // ── Cleanup ──────────────────────────────────
    if (swsCtx)    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    m_running = false;
    wxGetApp().CallAfter([this]() {
        UpdateStatus("Stream ended or disconnected.");
        m_frameTimer.Stop();
        SetConnectedState(false);
        m_fpsLabel->SetLabel("");
    });
}
