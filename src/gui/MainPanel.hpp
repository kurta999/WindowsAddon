#pragma once

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>

#include "interface/ISensorObserver.hpp"

class CryptoPrice;
class PathSeparator;
class PrintScreenSaver;
class CorsairHid;
class CanSerialPort;
class CustomMacro;
class DatabaseLogic;
class Server;
class Sensors;
class Settings;
class SerialPort;
class BsecHandler;

// !\brief What the main page reads, handed in by the composition root.
//
// This page displays the state of most of the application, so it has more
// collaborators than any other. They are named rather than passed positionally:
// a row of same-typed references is a list nobody can check, and getting two of
// them the wrong way round would compile.
struct MainPanelPorts
{
	CryptoPrice& crypto_price;
	PathSeparator& path_separator;
	PrintScreenSaver& screenshots;
	CorsairHid& corsair_hid;
	CanSerialPort& can_port;

	/* The measurement half of the page: the sensor readings, the graph window,
	   the crypto refresh interval and the air-quality index. */
	Sensors& sensors;
	DatabaseLogic& database;
	BsecHandler& bsec;
	Settings& settings;

	/* The status strip and the keyboard map. */
	Server& server;
	SerialPort& keyboard_port;
	CustomMacro& macros;
};

class MainPanel : public wxPanel, public ISensorObserver
{
public:
	MainPanel(wxFrame* parent, const MainPanelPorts& ports);

	// !\brief Fetch the prices if it is time, and redraw them if they changed.
	void RefreshCryptoPrices();

	void OnMeasurementUpdated(const Measurement& m, size_t recv_count) override;

	void UpdateKeybindings();
	void UpdateStatuses();
	void UpdateCryptoPrices(float eth_buy, float eth_sell, float btc_buy, float btc_sell);

	wxButton* m_RefreshButton = nullptr;
	wxButton* m_ResetButton = nullptr;
	wxButton* m_GenerateGraphs = nullptr;
	wxButton* m_ClearMeasurements = nullptr;
	wxStaticText* m_textTemp = nullptr;
	wxStaticText* m_textHum = nullptr;
	wxStaticText* m_textCO2 = nullptr;
	wxStaticText* m_textVOC = nullptr;
	wxStaticText* m_textCO = nullptr;
	wxStaticText* m_textPM25 = nullptr;
	wxStaticText* m_textPM10 = nullptr;
	wxStaticText* m_textPressure = nullptr;
	wxStaticText* m_textR = nullptr;
	wxStaticText* m_textG = nullptr;
	wxStaticText* m_textB = nullptr;
	wxStaticText* m_textLux = nullptr;
	wxStaticText* m_textCCT = nullptr;
	wxStaticText* m_textUV = nullptr;
	wxStaticText* m_textTime = nullptr;
	wxButton* m_OpenGraphs = nullptr;
	wxSpinCtrl* m_GraphStartHours1 = nullptr;
	wxSpinCtrl* m_GraphStartHours2 = nullptr;
	wxStaticText* m_EthPrice = nullptr;
	wxStaticText* m_BtcPrice = nullptr;
	wxButton* m_RefreshCrypto = nullptr;

private:

	std::map<std::string, wxStaticBox*> key_map;
	wxStaticText* m_CorsairDeviceName = nullptr;
	wxStaticLine* m_CorsairSeparatorLine_1 = nullptr;
	wxStaticLine* m_CorsairSeparatorLine_2 = nullptr;

	MainPanelPorts m_Ports;
	wxStaticText* m_TcpBackendStatus = nullptr;
	wxStaticText* m_KeyboardStatus = nullptr;
	wxStaticText* m_CanStatus = nullptr;
	wxStaticText* m_ModbusStatus = nullptr;
	wxDECLARE_EVENT_TABLE();
};