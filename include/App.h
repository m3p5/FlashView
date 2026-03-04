#pragma once
#include <wx/wx.h>

class App : public wxApp {
public:
    virtual bool OnInit() override;
};

wxDECLARE_APP(App);
