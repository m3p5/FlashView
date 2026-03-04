#pragma once
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/config.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

// Forward declaration
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

enum {
    ID_CONNECT    = wxID_HIGHEST + 1,
    ID_DISCONNECT,
    ID_SNAPSHOT,
    ID_FRAME_TIMER,
    ID_LIGHT
};

class VideoPanel : public wxPanel {
public:
    VideoPanel(wxWindow* parent);
    void SetFrame(const wxImage& img);
    void Clear();

private:
    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);

    wxImage   m_image;
    wxBitmap  m_bitmap;

    wxDECLARE_EVENT_TABLE();
};

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame();

private:
    // UI helpers
    void BuildUI();
    void LoadSettings();
    void SaveSettings();
    void UpdateStatus(const wxString& msg, bool error = false);
    void SetConnectedState(bool connected);

    // Event handlers
    void OnConnect(wxCommandEvent& evt);
    void OnDisconnect(wxCommandEvent& evt);
    void OnSnapshot(wxCommandEvent& evt);
    void OnLight(wxCommandEvent& evt);
    void OnFrameTimer(wxTimerEvent& evt);
    void OnClose(wxCloseEvent& evt);

    // RTSP thread
    void StreamThread();
    void StopStream();

    // UI widgets
    VideoPanel*  m_videoPanel;
    wxTextCtrl*  m_urlCtrl;
    wxButton*    m_connectBtn;
    wxButton*    m_disconnectBtn;
    wxButton*    m_snapshotBtn;
    wxButton*    m_lightBtn;
    wxStaticText* m_statusLabel;
    wxStaticText* m_fpsLabel;
    wxTimer      m_frameTimer;

    // Stream state
    std::thread       m_streamThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_hasNewFrame{false};
    std::mutex        m_frameMutex;
    wxImage           m_pendingFrame;
    std::string       m_streamUrl;  // set by main thread before launching stream thread
    bool              m_lightOn{false};

    // FPS tracking
    int    m_frameCount{0};
    long long m_fpsLastTime{0};
    double m_currentFps{0.0};

    wxDECLARE_EVENT_TABLE();
};
