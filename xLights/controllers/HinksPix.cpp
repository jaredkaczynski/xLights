/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/smeighan/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/smeighan/xLights/blob/master/License.txt
 **************************************************************/

#include "HinksPix.h"
#include "../models/Model.h"
#include "../outputs/OutputManager.h"
#include "../outputs/Output.h"
#include "../outputs/ControllerEthernet.h"
#include "../models/ModelManager.h"
#include "ControllerCaps.h"
#include "UtilFunctions.h"

#include "../xSchedule/wxJSON/jsonreader.h"
#include "../xSchedule/wxJSON/jsonwriter.h"

#include <curl/curl.h>

#include <wx/msgdlg.h>
#include <wx/sstream.h>
#include <wx/regex.h>
#include <wx/progdlg.h>

#include <log4cpp/Category.hh>

#pragma region HinksPixOutput
void HinksPixOutput::Dump() const
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("    Output %d Uni %d StartChan %d Pixels %d Dir %d Proto %d Nulls %d ColorOrder %d Brightness %d Gamma %d ControlerStartChannel %d ControlerEndChannel %d",
        output,
        universe,
        startChannel,
        pixels,
        direction,
        protocol,
        nullPixel,
        colorOrder,
        brightness,
        gamma,
        controlerStartChannel,
        controlerEndChannel
        );
}

void HinksPixOutput::SetConfig(wxString const& data)
{
    const wxArrayString config = Split(data, { ',' });
    if (config.size() != 10) {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Invalid config data '%s'", (const char*)data.c_str());
        return;
    }

    if (wxAtoi(config[0]) != output) {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Mismatched output ports data port:'%d' data:'%s'", output, (const char*)data.c_str());
        return;
    }
    if (config[1] != "undefined")
        protocol = wxAtoi(config[1]);
    else
        protocol = 0;
    controlerStartChannel = wxAtoi(config[2]);
    pixels = wxAtoi(config[3]);
    controlerEndChannel = wxAtoi(config[4]);
    direction = wxAtoi(config[5]);
    colorOrder = wxAtoi(config[6]);
    nullPixel = wxAtoi(config[7]);
    brightness = wxAtoi(config[8]);
    gamma = wxAtoi(config[9]);
}

wxString HinksPixOutput::BuildCommand() const
{
    return wxString::Format("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d|",
        output, protocol, controlerStartChannel, pixels, controlerEndChannel,
        direction, colorOrder, nullPixel, brightness, gamma);
}

void HinksPixOutput::setControllerChannels(const int startChan)  
{
    controlerStartChannel = startChan;
    int chanPerPix = 3;//RGB nodes
    if (colorOrder == 6 || colorOrder == 7)//RGBW nodes
        chanPerPix = 4;
    controlerEndChannel = controlerStartChannel + (pixels * chanPerPix) - 1;
}
#pragma endregion

#pragma region HinksPixSerial
void HinksPixSerial::Dump() const
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("   E131 Uni %d E131 StartChan %d E131 NumOfChan %d E131 Enabled %d Mode %d DDP StartChan %d DDP NumOfChan %d DPP Enabled %d Upload %d",
        e131Universe,
        e131StartChannel,
        e131NumOfChan,
        e131Enabled,
        mode,
        ddpStartChannel,
        ddpNumOfChan,
        ddpEnabled,
        upload
    );
}

void HinksPixSerial::SetConfig(std::map<wxString, wxString> const& data)
{
    if (data.size() != 8) {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Invalid serial config map data");
        return;
    }

    mode = wxAtoi(data.at("A"));

    e131Enabled = wxAtoi(data.at("B"));
    e131Universe = wxAtoi(data.at("D"));
    e131StartChannel = wxAtoi(data.at("E"));
    e131NumOfChan = wxAtoi(data.at("F"));

    ddpEnabled = wxAtoi(data.at("C"));
    ddpStartChannel = wxAtoi(data.at("G"));
    ddpNumOfChan = wxAtoi(data.at("H"));
}

wxString HinksPixSerial::BuildCommand() const
{
    return wxString::Format("A,%d,B,%d,C,%d,D,%d,E,%d,F,%d,G,%d,H,%d",
        mode, e131Enabled, ddpEnabled, e131Universe, e131StartChannel,
        e131NumOfChan, ddpStartChannel, ddpNumOfChan);
}
#pragma endregion

#pragma region HinksPixSmart
void HinksSmartOutput::Dump() const
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("  ID %d Type %d Port 1 Start Pixel %d Port 2 Start Pixel %d Port 3 Start Pixel %d Port 4 Start Pixel %d",
        id,
        type,
        portStartPixel[0],
        portStartPixel[1],
        portStartPixel[2],
        portStartPixel[3]
    );
}

void HinksSmartOutput::SetConfig(wxString const& data)
{
    const wxArrayString config = Split(data, { ',' });
    if (config.size() != 6) {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Invalid config data '%s'", (const char*)data.c_str());
        return;
    }

    id = wxAtoi(config[0]);
    type = wxAtoi(config[1]);
    portStartPixel[0] = wxAtoi(config[2]);
    portStartPixel[1] = wxAtoi(config[3]);
    portStartPixel[2] = wxAtoi(config[4]);
    portStartPixel[3] = wxAtoi(config[5]);
}

wxString HinksSmartOutput::BuildCommand() const
{
    //{"V":"1,0,51,51,51,51"}
    return wxString::Format("{\"V\":\"%d,%d,%d,%d,%d,%d,\"}",
        id, type, portStartPixel[0], portStartPixel[1],
        portStartPixel[2], portStartPixel[3]);
}
#pragma endregion

#pragma region Private Functions
bool HinksPix::InitControllerOutputData()
{
    _pixelOutputs.clear();

    for (int i = 0; i < 48; i++) {
        _pixelOutputs.push_back(HinksPixOutput (i+1));
    }

    //0 is nothing, 1 is SPI board, 2 is differental expansion, 3 is AC board(not supported)
    if (_outputTypes[0] == 1 || _outputTypes[0] == 2) {
        InitExpansionBoardData(1, 1, 16);
    }

    if(_outputTypes[1] == 1 || _outputTypes[1] == 2) {
        InitExpansionBoardData(2, 17, 16);
    }

    if(_outputTypes[2] == 1 || _outputTypes[2] == 2) {
        InitExpansionBoardData(3, 33, 16);
    }

    _serialOutput = InitSerialData();

    return true;
}

void HinksPix::InitExpansionBoardData(int expansion, int startport, int length)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    
    //Expansion Board board "row" 'getting' commands are 1041, 1042, 1043 for expansion 1,2,3
    //this gets all 16 ports configs seporated by a pipe |
    wxString const data = GetControllerData(1040 + expansion);
    const wxArrayString portdata = Split(data, { '|' });

    if (portdata.size() != length) {
        logger_base.error("Data from controller size and Expansion Size don't match");
        logger_base.error(data);
        return;
    }

    for (int i = 0; i < length; i++) {
        _pixelOutputs[(startport - 1) + i].SetConfig(portdata[i]);
    }
}

std::unique_ptr < HinksPixSerial> HinksPix::InitSerialData()
{
    std::unique_ptr < HinksPixSerial> serial = std::make_unique<HinksPixSerial>();
    auto const data = GetControllerData(908);//get serial config command

    if (!data.empty()) {
        auto const serialConfig = StringToMap(data);
        serial->SetConfig(serialConfig);
    }
    else {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Invalid Return data %s", (const char*)data.c_str());
    }

    return serial;
}

void HinksPix::UploadPixelOutputs(bool& worked)
{
    if (_outputTypes[0] == 1 || _outputTypes[0] == 2) {
        UploadExpansionBoardData(1, 1, 16, worked);
    }

    if (_outputTypes[1] == 1 || _outputTypes[1] == 2) {
        UploadExpansionBoardData(2, 17, 16, worked);
    }

    if (_outputTypes[2] == 1 || _outputTypes[2] == 2) {
        UploadExpansionBoardData(3, 33, 16, worked);
    }
}

void HinksPix::UploadExpansionBoardData(int expansion, int startport, int length, bool& worked)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));

    logger_base.debug("Building pixel upload Expansion %d:", expansion);
    wxString requestString;

    for (int i = 0; i < length; i++) {
        _pixelOutputs[(startport - 1) + i].Dump();
        requestString += _pixelOutputs[(startport - 1) + i].BuildCommand();
    }
    requestString += "||";

    logger_base.info("Set String Output Information for Expansion %d.", expansion);

    //Expansion Board board "row" 'setting' commands are 3041, 3042, 3043 for expansion 1,2,3
    auto const pixelRet = GetControllerData(3040 + expansion, requestString);
    if (pixelRet != "done") {
        logger_base.error("%d Return %s", 3040 + expansion, (const char*)pixelRet.c_str());
        worked = false;
    }
}

void HinksPix::UpdatePortData(HinksPixOutput &pd, UDControllerPort* stringData, std::map<int, int> const& uniChan) const
{
    int const protocol = EncodeStringPortProtocol(stringData->GetFirstModel()->GetProtocol());
    if (protocol != -1) {
        pd.protocol = protocol;
    }

    int const nullPix = stringData->GetFirstModel()->GetNullPixels(-1);
    if (nullPix != -1) {
        pd.nullPixel = nullPix;
    }

    int const brightness = stringData->GetFirstModel()->GetBrightness(-1);
    if (brightness != -1) {
        pd.brightness = EncodeBrightness(brightness);
    }

    std::string const direction = stringData->GetFirstModel()->GetDirection("unknown");
    if (direction != "unknown" ) {
        pd.direction = EncodeDirection(direction);
    }

    int const gamma = stringData->GetFirstModel()->GetGamma(-1);
    if (gamma != -1) {
        pd.gamma = gamma;
    }

    std::string const color = stringData->GetFirstModel()->GetColourOrder("");
    if (!color.empty()) {
        pd.colorOrder = EncodeColorOrder(color);
    }
    pd.universe = stringData->GetUniverse();
    pd.startChannel = stringData->GetUniverseStartChannel();
    pd.pixels = stringData->Pixels();

    pd.setControllerChannels( CalcControllerChannel(pd.universe, pd.startChannel, uniChan));
}

void HinksPix::UpdateSerialData( HinksPixSerial & pd, UDControllerPort* serialData, int mode) const
{
    if (pd.mode != mode) {
        pd.mode = mode;
        pd.upload = true;
    }

    int dmxOffset = 1;
    UDControllerPortModel* m = serialData->GetFirstModel();
    if (m != nullptr) {
        dmxOffset = m->GetDMXChannelOffset();
        if (dmxOffset < 1 || dmxOffset > 512) dmxOffset = 1; // a value less than 1 makes no sense
    }

    const int sc = serialData->GetStartChannel();
    const int usc = serialData->GetUniverseStartChannel() - dmxOffset + 1; // it would be good if these functions behaved more like GetStartChannel does on serial ports 
    int maxChan = serialData->GetEndChannel() - sc + 1;

    if (maxChan < 16) maxChan = 16;

    if (pd.mode != 1) {//0 and 2 are E131 and Artnet
        if (pd.e131Universe != (serialData->GetUniverse())) {
            pd.e131Universe = serialData->GetUniverse();
            pd.upload = true;
        }

        if (pd.e131StartChannel != usc) {
            pd.e131StartChannel = usc;
            pd.upload = true;
        }

        if (pd.e131NumOfChan != maxChan) {
            pd.e131NumOfChan = maxChan;
            pd.upload = true;
        }

        if (!pd.e131Enabled) {
            pd.e131Enabled = true;
            pd.upload = true;
        }
    } else {//1 is DDP
        if (pd.ddpStartChannel != sc) {
            pd.ddpStartChannel = sc;
            pd.upload = true;
        }
        if (pd.ddpNumOfChan != maxChan) {
            pd.ddpNumOfChan = maxChan;
            pd.upload = true;
        }

        if (!pd.ddpEnabled) {
            pd.ddpEnabled = true;
            pd.upload = true;
        }
    }
}

void HinksPix::UploadSmartRecievers(bool& worked) 
{
    int exp = 0;
    for (auto const & expan: _smartOutputs)
    {
        int bnk = 0;
        for (auto const& bank : expan)
        {
            UploadSmartRecieverData(exp, bnk, _smartOutputs[exp][bnk], worked);
            ++bnk;
        }
        ++exp;
    }
}

void HinksPix::UploadSmartRecieverData(int expan, int bank, std::vector<HinksSmartOutput> const& receivers, bool& worked)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));

    logger_base.debug("Building SmartReciever upload Expansion %d Bank %d:", expan, bank);
    if (receivers.size() == 0) {
        logger_base.info("No SmartReciever found");
        return;
    }
    //{"CMD":"SCONFIG","BOARD":"0","Port4":"0","LIST":[{"V":"0,1,1,1,1,1"},{"V":"1,0,51,51,51,51"},{"V":"2,0,101,101,101,101"},{"V":"3,0,151,151,151,151"},{"V":"6,2,0,1,0,0"},{"V":"8,0,201,1,1,1"}]}
    wxString requestString = wxString::Format("{\"CMD\":\"SCONFIG\",\"BOARD\":\"%d\",\"Port4\":\"%d\",\"LIST\":[",
        expan, bank);
    
    for (int i = 0; i < receivers.size(); i++) {
        receivers[i].Dump();
        requestString += receivers[i].BuildCommand();
        requestString += ",";
    }
    requestString.RemoveLast();//remove last ","
    requestString += "]}";

    auto const ret = GetJSONControllerData(GetJSONPostURL(), requestString);
    if (ret.find("\"OK\"") == std::string::npos) {
        logger_base.error("Failed Return %s", (const char*)ret.c_str());
        worked = false;
    }
}

void HinksPix::CalculateSmartRecievers(UDControllerPort* stringData)
{
    if (!stringData->AtLeastOneModelIsUsingSmartRemote())
        return;
    int port = stringData->GetPort() - 1;
    int expansionBoard = port / 16;
    int expansionPort = port % 16;
    int bank = expansionPort / 4;
    int subPort = (expansionPort % 4);
    int32_t portStartChan = stringData->GetStartChannel();
    for (const auto& it : stringData->GetModels()) {
        if (it->GetSmartRemote() > 0) {
            int id = it->GetSmartRemote() - 1;
            int32_t startChan = it->GetStartChannel();
            auto smartOut = std::find_if(_smartOutputs[expansionBoard][bank].begin(), _smartOutputs[expansionBoard][bank].end(), [id](auto const& so) {
                    return so.id == id;
                });
            if (smartOut != _smartOutputs[expansionBoard][bank].end()) {
                smartOut->portStartPixel[subPort] = ((startChan - portStartChan) / it->GetChannelsPerPixel()) + 1;
            } else {
                auto & smartPort = _smartOutputs[expansionBoard][bank].emplace_back(id);
                if (it->GetSmartRemoteType().find("16") != std::string::npos && ((id % 4) == 0 ))
                    smartPort.type = 1;
                smartPort.portStartPixel[subPort] = ((startChan - portStartChan) / it->GetChannelsPerPixel()) + 1;
            }
        }
    }
}

//Pixels Outputs use a controller start channel, this maps xlights uni/start chan to the controller channel
int HinksPix::CalcControllerChannel(int universe,int startChan, std::map<int, int> const& uniChan) const
{
    if (!uniChan.count(universe))
    {
        static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
        logger_base.error("Unknown Universe: %d Reupload your E131 inputs", universe);
        return 1;
    }
    int chan = uniChan.at(universe);
    chan += startChan;
    chan--;         //dont count the first channel in offset calc
    return chan;
}

//Most data uses a GET .cgi endpoint
wxString HinksPix::GetControllerData(int rowIndex, std::string const& data)
{
    return GetControllerRowData(rowIndex, GetInfoURL(), data);
}

//E131 data uses a different .cgi endpoint
wxString HinksPix::GetControllerE131Data(int rowIndex)
{
    return GetControllerRowData(rowIndex, GetE131URL(), std::string());
}

//all of the Controller data is retrieved/set by "GET"ing different ROW values
wxString HinksPix::GetControllerRowData(int rowIndex, std::string const& url, std::string const& data)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    std::string res;
    std::string const baseIP = _fppProxy.empty() ? _ip : _fppProxy;

    logger_base.debug("Making request to HinksPix '%s'.", (const char*)url.c_str());

    CURL* curl = curl_easy_init();
    struct curl_slist* list = NULL;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, std::string("http://" + baseIP + _baseUrl + url).c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20);
       
        list = curl_slist_append(list, "Content-type: text/plain");
        logger_base.debug("Row='%d'.", rowIndex);
        list = curl_slist_append(list, std::string("ROW: " + std::to_string(rowIndex)).c_str());
        
        if (!data.empty()) {
            list = curl_slist_append(list, std::string("DATA: " + data).c_str());
            logger_base.debug("DATA='%s'.", (const char*)data.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        std::string response_string;

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);

        /* Perform the request, res will get the return code */
        CURLcode r = curl_easy_perform(curl);

        if (r != CURLE_OK) {
            logger_base.error("Failure to access %s: %s.", (const char*)url.c_str(), curl_easy_strerror(r));
        }
        else {
            res = response_string;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    return res;
}

//all of the Controller data is retrieved/set by "GET"ing different values
std::string HinksPix::GetJSONControllerData(std::string const& url, std::string const& data)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    std::string res;
    std::string const baseIP = _fppProxy.empty() ? _ip : _fppProxy;

    logger_base.debug("Making request to HinksPix '%s'.", (const char*)url.c_str());

    CURL* curl = curl_easy_init();
    struct curl_slist* list = NULL;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, std::string("http://" + baseIP + _baseUrl + url).c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20);

        list = curl_slist_append(list, "Content-type: text/plain");

        if (!data.empty()) {
            list = curl_slist_append(list, std::string("DATA: " + data).c_str());
            logger_base.debug("DATA='%s'.", (const char*)data.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        std::string response_string;

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);

        /* Perform the request, res will get the return code */
        CURLcode r = curl_easy_perform(curl);

        if (r != CURLE_OK) {
            logger_base.error("Failure to access %s: %s.", (const char*)url.c_str(), curl_easy_strerror(r));
        }
        else {
            res = response_string;
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    return res;
}

bool HinksPix::GetControllerDatasJSON(const std::string& url, std::string const& data, wxJSONValue& val) {
    std::string const sval = GetJSONControllerData(url, data);
    if (!sval.empty()) {
        wxJSONReader reader;
        reader.Parse(sval, &val);
        return true;
    }
    return false;
}

//create a map of E131 channels to the local controller channels  
std::map<int, int> HinksPix::MapE131Addresses()
{
    std::map<int, int> uniToChan;
    auto const data = GetControllerData(902);
    if (data.empty())
        return uniToChan;

    const auto map = StringToMap(data);

    int numbOfUni = wxAtoi(map.at("D"));

    int calls = (int)std::ceil((float)numbOfUni / 6);

    for (int i = 0; i < calls; ++i) {
        wxString const data = GetControllerE131Data(i);
        const wxArrayString items = Split(data, { ',' });
        if (items.size() % 6 != 0)
            continue;

        for (int j = 0; j < items.size(); j += 6) {
            int univ = wxAtoi(items[j + 1]);
            int contChan = wxAtoi(items[j + 4]);
            if (univ == 0 && contChan == 0)
                continue;
            uniToChan.insert({ univ, contChan });
        }
    }
    return uniToChan;
}

std::map<wxString, wxString> HinksPix::StringToMap(wxString const& text) const
{
    std::map<wxString, wxString> map;
    const wxArrayString items = Split(text, {','});
    if(items.size() % 2 == 0) {
        for (int i =0; i < items.size() - 1; i += 2) {
            map[items[i]] = items[i + 1];
        }
    }
    return map;
}
#pragma endregion

#pragma region Encode and Decode
int HinksPix::EncodeStringPortProtocol(const std::string& protocol) const
{
    wxString p(protocol);
    p = p.Lower();

    if (p == "ws2811") return 1;
    if (p == "ws2812") return 2;//unused
    if (p == "ws2812b") return 3;//unused
    if (p == "ws2813") return 4;//unused
    if (p == "ws2801") return 5;
    if (p == "tls3001") return 6;
    if (p == "apa102") return 7;
    wxASSERT(false);
    return -1;
}

int HinksPix::EncodeColorOrder(const std::string& colorOrder) const
{
    wxString c(colorOrder);
    c = c.Lower();

    if (c == "rgb") return 0;
    if (c == "rbg") return 1;
    if (c == "grb") return 2;
    if (c == "gbr") return 3;
    if (c == "brg") return 4;
    if (c == "bgr") return 5;
    if (c == "rgbw") return 6;
    if (c == "wrgb") return 7;
    wxASSERT(false);
    return -1;
}

int HinksPix::EncodeDirection(const std::string& direction) const
{
    return direction == "Reverse";
}

int HinksPix::EncodeBrightness(int brightness) const
{
    //HinkPix Allows brightness values of 100,90,80,70,60,50,40,30,20,15
    const int newVal = (brightness / 10) * 10;//round to the 10's
    if (newVal < 20)
        return 15;
    return newVal;
}

int HinksPix::EncodeGamma(int gamma) const
{
    //HinkPix Allows gamma value of 1-4 as integer values
    if (gamma > 4)
        return 4;
    return gamma;
}

#pragma endregion

#pragma region Constructors and Destructors
HinksPix::HinksPix(const std::string& ip, const std::string& proxy) : BaseController(ip, proxy), _numberOfOutputs(48), _Flex(false)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    
    //Get Controller Info
    wxString  data;

    for (int x = 0; x < 3; x++)
    {
        data = GetControllerData(1100);
        if (!data.empty())
        {
            break;
        }
    }

    if (!data.empty()) {
        auto const controlInfo = StringToMap(data);

        //get output type options
        _outputTypes[0] = wxAtoi(controlInfo.at("A"));
        _outputTypes[1] = wxAtoi(controlInfo.at("B"));
        _outputTypes[2] = wxAtoi(controlInfo.at("C"));
        _connected = true;
        _Flex = wxAtoi(controlInfo.at("E"));

        if (!_Flex)
            _numberOfOutputs = 16;
    }
    else {
        _connected = false;
        logger_base.error("Error connecting to HinksPix controller on %s.", (const char*)_ip.c_str());
        DisplayError(wxString::Format("Error connecting to HinksPix controller on %s.", _ip).ToStdString());
    }

    if (_connected) {
        //Get Controller Firmware
        auto const firmData = GetControllerData(906);
        if (!firmData.empty()) {
            static wxRegEx firmwareregex("Main\\sCPU\\s(\\d+)\\s+Power\\sCPU\\s+(\\d+)\\s+WIFI\\sCPU\\s+(\\d+)\\s+Web\\s+(\\d+)", wxRE_ADVANCED | wxRE_NEWLINE);
            if (firmwareregex.Matches(firmData)) {
                _version = wxString::Format("MAIN:%s,POWER:%s,WIFI:%s,WEB:%s",
                    firmwareregex.GetMatch(firmData, 1),
                    firmwareregex.GetMatch(firmData, 2),
                    firmwareregex.GetMatch(firmData, 3),
                    firmwareregex.GetMatch(firmData, 4));
            }
        } 
        else {
            logger_base.error("Error Get Firmware Info");
        }

        if (!_Flex)
            _model = "EasyLights Pix16";
        _model = "HinksPix PRO";

        logger_base.debug("Connected to HinksPix controller model %s.", (const char*)GetFullName().c_str());
    }
}

HinksPix::~HinksPix()
{
    _pixelOutputs.clear();
}
#pragma endregion

#pragma region Getters and Setters
bool HinksPix::SetInputUniverses(ControllerEthernet* controller, wxWindow* parent)
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("HinksPix Inputs Upload: Uploading to %s", (const char*)_ip.c_str());

    wxString request;

    // Get universes based on IP
    std::list<Output*> outputs = controller->GetOutputs();

    auto const data = GetControllerData(902);
    if (data.empty())
        return false;

    const auto map = StringToMap(data);

    int maxUnv = wxAtoi(map.at("C"));

    if (controller->GetOutputCount() > maxUnv) {
        DisplayError(wxString::Format("Attempt to upload %d universes to HinksPix controller but only %d are supported.", controller->GetOutputCount(), maxUnv).ToStdString());
        return false;
    }

    auto out = outputs.front();
    int type = 0;//e131=0, ddp=1, artnet=2
    int multi = 0;
    int DDPStart = 0;
    if (out->GetType() == OUTPUT_E131) {
        type = 0;
    }
    else if (out->GetType() == OUTPUT_ARTNET) {
        type = 2;
    }
    else if (out->GetType() == OUTPUT_DDP) {
        type = 1;
        DDPStart = out->GetStartChannel();
    }

    if (out->GetIP() == "MULTICAST") {
        multi = 1;
    }

    auto const cmd = wxString::Format("A,%d,B,%d,C,%d,D,%d,E,%d",
        multi, type, maxUnv, controller->GetOutputCount(), DDPStart);

    //Set Controller Input mode
    auto const setRet = GetControllerData(4902, cmd);
    if (setRet != "done")
    {
        logger_base.error("4902 Return %s", (const char*)setRet.c_str());
        DisplayError("Changed HinksPix Input Mode FAILED.");
        return false;
    }

    //if DDP, skip the next part
    if (type == 1)
        return true;

    //Joes code allows 6 universe settings uploaded at a time
    //loop though and submit every 6

    int numberOfCalls;
    if (maxUnv <= 65) {
        numberOfCalls = 11;
    }
    else {
        numberOfCalls = 25;
    }

    int index = 1;
    int cntr_start = 1;

    bool worked = true;

    auto it = outputs.begin();
    for (int j = 0; j < numberOfCalls; j++) {
        bool post = false;
        wxString msg = wxString::Format("ROWCNT=16:ROW=%d:", j);
        for (int i = 0; i < 6; i++) {
            if (it != outputs.end()) {
                if (i != 0)
                    msg += ",";
                msg += wxString::Format("%d,%d,%d,1,%d,%d", index,
                    (*it)->GetUniverse(), (*it)->GetChannels(), cntr_start,
                    (cntr_start + (*it)->GetChannels() - 1));
                cntr_start += (*it)->GetChannels();
                it++;
                index++;
                post = true;
            }
            else {
                msg += ",0,0,0,0,0,0";
            }
        }
        //post data
        if (post) {
            auto const res = GetControllerData(2001, msg);
            if (res != "done")
                worked = false;
        }
    }

    //reboot
    auto const resetres = GetControllerData(1111);
    if (resetres != "done")
        worked = false;

    if (!worked)
        DisplayError("HinksPix E131 Input command FAILED.");

    return worked;
}

bool HinksPix::SetOutputs(ModelManager* allmodels, OutputManager* outputManager, ControllerEthernet* controller, wxWindow* parent)
{
    wxProgressDialog progress("Uploading ...", "", 100, parent, wxPD_APP_MODAL | wxPD_AUTO_HIDE);
    progress.Show();

    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("HinksPix Outputs Upload: Uploading to %s", (const char*)_ip.c_str());

    // Get universes based on IP
    std::list<Output*> outputs = controller->GetOutputs();

    auto o = outputs.front();

    int mode = 0;

    if (o->GetType() == OUTPUT_E131) {
        mode = 0;
    }
    else if (o->GetType() == OUTPUT_ARTNET) {
        mode = 2;
    }
    else if (o->GetType() == OUTPUT_DDP) {
        mode = 1;
    }

    progress.Update(0, "Scanning models");
    logger_base.info("Scanning models.");

    std::string check;
    UDController cud(controller, outputManager, allmodels, check, false);

    //first check rules
    auto rules = ControllerCaps::GetControllerConfig(controller);
    const bool success = cud.Check(rules, check);

    logger_base.debug(check);

    cud.Dump();
    if (!success) {
        DisplayError("HinksPix Upload Error:\n" + check, parent);
        progress.Update(100, "Aborting.");
        return false;
    }
    bool worked = true;

    logger_base.info("Calculating Universe Start Channel Mappings.");
    progress.Update(10, "Calculating Universe Start Channel Mappings.");

    //set DDP mode first
    if (mode == 1) {
        SetInputUniverses(controller, parent);
    }

    std::map<int, int> map;
    if (mode == 1) {
        map.insert({ o->GetUniverse(),o->GetStartChannel() });
    }
    else {
        map = MapE131Addresses();
    }

    logger_base.info("Initializing Pixel Output Information.");
    progress.Update(20, "Initializing Universe Start Channel Mappings.");

    InitControllerOutputData();

    logger_base.info("Figuring Out Pixel Output Information.");
    progress.Update(30, "Figuring Out Pixel Output Information.");

    //loop to setup string outputs
    for (int port = 1; port <= GetNumberOfOutputs(); port++) {
        if (cud.HasPixelPort(port)) {
            UDControllerPort* portData = cud.GetControllerPixelPort(port);
            auto pixOut = std::find_if(_pixelOutputs.begin(), _pixelOutputs.end(), [port](auto const& po) {return po.output == port; });
            if (pixOut != _pixelOutputs.end())
                UpdatePortData(*pixOut, portData, map);

            CalculateSmartRecievers(portData);
        }
    }

    logger_base.info("Uploading SmartRecievers Information.");
    progress.Update(40, "Uploading SmartRecievers Information.");
    UploadSmartRecievers(worked);

    logger_base.info("Uploading String Output Information.");
    progress.Update(50, "Uploading String Output Information.");
    UploadPixelOutputs(worked);

    logger_base.info("Figuring Out DMX Output Information.");
    progress.Update(70, "Figuring Out DMX Output Information.");

    if (cud.HasSerialPort(1)) {
        UDControllerPort* portData = cud.GetControllerSerialPort(1);
        UpdateSerialData(*_serialOutput, portData, mode);
    }

    logger_base.info("Uploading DMX Output Information.");
    progress.Update(80, "Uploading DMX Output Information.");

    _serialOutput->Dump();
    if (_serialOutput->upload) {
        const std::string serialRequest = _serialOutput->BuildCommand();
        auto const serRet = GetControllerData(4908, serialRequest);
        if (serRet != "done") {
            logger_base.error("4908 Return %s", (const char*)serRet.c_str());
            worked = false;
        }
    }

    //reboot
    logger_base.info("Rebooting Controller.");
    progress.Update(90, "Rebooting Controller.");
    auto const resetres = GetControllerData(1111);
    if (resetres != "done")
        worked = false;

    progress.Update(100, "Done.");
    return worked;
}
#pragma endregion 

