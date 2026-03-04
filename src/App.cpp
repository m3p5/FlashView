#include "App.h"
#include "MainFrame.h"

wxIMPLEMENT_APP(App);

bool App::OnInit() {
    if (!wxApp::OnInit()) return false;
    SetAppName("FlashView");
    SetAppDisplayName("FlashView");

    MainFrame* frame = new MainFrame();
    frame->Show(true);
    return true;
}
