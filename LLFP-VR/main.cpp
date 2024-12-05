#include <shlobj.h>
#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/GameReferences.h"
#include "f4se/GameRTTI.h"
#include "f4se/ScaleformValue.h"
#include "f4se/Hooks_Scaleform.h"
#include "f4se/ScaleformCallbacks.h"
#include "LL_FourPlay.h"
#include <algorithm>

using namespace std;
#pragma comment(lib,"Winmm.lib")

// Store plugin log in My Games, differentiating between various builds
UInt32 pluginVersion = PLUGIN_VERSION;
char pluginName[] = { PLUGIN_NAME };
char pluginAuthor[] = { PLUGIN_AUTHOR };

char pluginExt[] = {".log"};
char pluginLogPath[] = {"\\My Games\\Fallout4VR\\F4SE\\"};
#ifdef _DEBUG
	char pluginEdition[] = {"-debug"};
#else
	char pluginEdition[] = {""};
#endif
#ifdef _NOGORE
	char pluginVariant[] = {"-nogore"};
#else
	char pluginVariant[] = {""};
#endif
static char pluginLog[sizeof(pluginLogPath)+sizeof(pluginName)+sizeof(pluginEdition)+sizeof(pluginVariant)+sizeof(pluginExt)];
char pluginCustomIni[sizeof(pluginName)+sizeof(".ini")];

PluginHandle	g_pluginHandle = kPluginHandle_Invalid;

F4SEScaleformInterface		* g_scaleform = NULL;
F4SEPapyrusInterface		* g_papyrus = NULL;
F4SESerializationInterface	* g_serialization = NULL;
F4SEMessagingInterface		* g_messaging = NULL;

#define REQUIRED_RUNTIME CURRENT_RELEASE_RUNTIME

// Build the pluginLog name and open it
void OpenPluginLog()
{
	/*gLog.OpenRelative(CSIDL_MYDOCUMENTS, R"(\\My Games\\Fallout4VR\\F4SE\\LL_fourPlay_VR.log)");
	gLog.SetPrintLevel(IDebugLog::kLevel_DebugMessage);
	gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);*/

	strcpy_s(pluginLog, pluginLogPath);
	strcat_s(pluginLog, pluginName);
	strcat_s(pluginLog, pluginVariant);
	strcat_s(pluginLog, pluginEdition);
	strcat_s(pluginLog, pluginExt);

	gLog.OpenRelative(CSIDL_MYDOCUMENTS, pluginLog);
#ifdef _DEBUG
	gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);
#endif
}

/**** runtime code ****/

/**** helpers ****/

const std::string & GetCustomConfigPath(const char * name);
std::string GetCustomConfigOption(const char * name, const char * section, const char * key);
bool GetCustomConfigOption_UInt32(const char * name, const char * section, const char * key, UInt32 * dataOut);
bool SetCustomConfigOption(const char * name, const char * section, const char * key, const char * value);
bool SetCustomConfigOption_UInt32(const char * name, const char * section, const char * key, UInt32 data);

#define maxResultBuf 32768

/* modified from Utilities.h */

const std::string & GetCustomConfigPath(const char * name)
{
	static std::string s_configPath;

	std::string	runtimePath = GetRuntimeDirectory();
	if(!runtimePath.empty())
	{
		s_configPath  = runtimePath;
		s_configPath += "Data\\F4SE\\plugins\\";
		s_configPath += name;

		_DMESSAGE("config path = %s", s_configPath.c_str());
	}

	return s_configPath;
}

std::string GetCustomConfigOption(const char * name, const char * section, const char * key)
{
	std::string	result;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty())
	{
		char	resultBuf[maxResultBuf];
		resultBuf[0] = 0;

		UInt32	resultLen = GetPrivateProfileString(section, key, NULL, resultBuf, sizeof(resultBuf), configPath.c_str());

		result = resultBuf;
	}

	return result;
}

bool SetCustomConfigOptions(const char * name, const char * section, VMArray<BSFixedString> keys, VMArray<BSFixedString> values)
{
	bool result = false;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty() && (keys.Length() == values.Length()))
	{
		for (int i = 0; i < keys.Length(); ++i)
		{
			BSFixedString k;
			BSFixedString v;
			keys.Get(&k, i);
			values.Get(&v, i);
			result = WritePrivateProfileString(section, k.c_str(), v.c_str(), configPath.c_str()) ? true : false;
			if (!result)
				break;
		}
	}

	return result;
}

bool SplitKeyValue(const char * keyValue, BSFixedString & key, BSFixedString & value)
{
		std::string t = keyValue;
		std::string::size_type pos = t.find_first_of('=', 0);
		t.erase(pos, t.length());
		if (t.length() > 0)
		{
			key = t.c_str();

			t = keyValue;
			pos = t.find_first_of('=', 0);
			t.erase(0, ++pos);
			value = t.c_str();

			return true;
		}
		else
			return false;
}

bool GetCustomConfigOptions(const char * name, const char * section, VMArray<BSFixedString> & keys, VMArray<BSFixedString> & values)
{
	bool result = false;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty() && section && !keys.IsNone())
	{
		char resultBuf[maxResultBuf];
		resultBuf[0] = 0;

		UInt32	resultLen = GetPrivateProfileSection(section, resultBuf, sizeof(resultBuf), configPath.c_str());
		char * begin = resultBuf;
		VMArray<BSFixedString> tk, tv;
		for (;*begin;++begin)
		{
			BSFixedString k, v;
			if (begin && (result = SplitKeyValue(begin, k, v)))
			{
				tk.Push(&k);
				tv.Push(&v);
				begin += strlen(begin);
			}
			else
				break;
		}
		keys = tk;
		if (!values.IsNone())
			values = tv;
	}

	return result;
}

VMArray<VMVariable> StringArrayToVarArray(VMArray<BSFixedString> str)
{
	VMVariable v;
	VMArray<VMVariable> va;
	for (int i = 0; i < str.Length(); i++)
	{
		BSFixedString s;
		str.Get(&s, i);
		v.Set(&s);
		va.Push(&v);
	}
	return va;
}

VMVariable StringArrayToVar(VMArray<BSFixedString> str)
{
	VMVariable v;
	VMArray<VMVariable> va = StringArrayToVarArray(str);
	v.Set<VMArray<VMVariable>>(&va);
	return v;
}

VMArray<VMVariable> GetCustomConfigOptions(const char * name, const char * section)
{
	VMArray<VMVariable> a;
	VMArray<BSFixedString> tk, tv;
	bool result = GetCustomConfigOptions(name, section, tk, tv);
	if (result)
	{
		VMVariable v;
		VMArray<VMVariable> va;
		v = StringArrayToVar(tk);
		a.Push(&v);
		v = StringArrayToVar(tv);
		a.Push(&v);
	}
	return a;
}

VMArray<BSFixedString> GetCustomConfigSections(const char * name)
{
	VMArray<BSFixedString>	result;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty())
	{
		char	resultBuf[maxResultBuf];
		resultBuf[0] = 0;

		UInt32	resultLen = GetPrivateProfileSectionNames(resultBuf, sizeof(resultBuf), configPath.c_str());
		char * begin = resultBuf;
		for (;*begin;++begin)
		{
			BSFixedString l = begin;
			result.Push(&l);
			begin += strlen(begin);
		}
	}

	return result;
}

bool ResetCustomConfigOptions(const char * name, const char * section, VMArray<BSFixedString> keys, VMArray<BSFixedString> values)
{
	bool result = false;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty() && (keys.Length() == values.Length()))
	{
		char	sendBuf[maxResultBuf];
		sendBuf[0] = 0;

		char * begin = sendBuf;
		int used = 1;	// at least the final terminator
		for (int i = 0; i < keys.Length(); ++i)
		{
			BSFixedString v;
			keys.Get(&v, i);
			const char * d = v.c_str();
			int l = strlen(d);
			used += l + 1;
			if (maxResultBuf>used)
			{
				strcat_s(begin, maxResultBuf-used, d);
				begin += strlen(d);
				strcat_s(begin, maxResultBuf-used-1, "=");
				begin += 1;
			}
			else
				return false;
			values.Get(&v, i);
			d = v.c_str();
			l = strlen(d);
			used += l + 1;
			if (maxResultBuf>used)
			{
				strcat_s(begin, maxResultBuf-used, d);
				begin += strlen(d)+1;
				*begin = 0;
			}
			else
				return false;
		}
		sendBuf[used] = 0;
		result = WritePrivateProfileSection(section, sendBuf, configPath.c_str()) ? true : false;
	}

	return result;
}

bool GetCustomConfigOption_UInt32(const char * name, const char * section, const char * key, UInt32 * dataOut)
{
	std::string	data = GetCustomConfigOption(name, section, key);
	if(data.empty())
		return false;

	return (sscanf_s(data.c_str(), "%u", dataOut) == 1);
}

bool GetCustomConfigOption_float(const char * name, const char * section, const char * key, float * dataOut)
{
	std::string	data = GetCustomConfigOption(name, section, key);
	if(data.empty())
		return false;

	return (sscanf_s(data.c_str(), "%g", dataOut) == 1);
}

bool SetCustomConfigOption(const char * name, const char * section, const char * key, const char * value)
{
	bool result = false;

	const std::string & configPath = GetCustomConfigPath(name);
	if(!configPath.empty())
		result = WritePrivateProfileString(section, key, value, configPath.c_str()) ? true : false;

	return result;
}

bool SetCustomConfigOption_UInt32(const char * name, const char * section, const char * key, UInt32 data)
{
	char value[65536] = "";
	if (!_itoa_s(data, value, 65535, 10))
		return SetCustomConfigOption(name, section, key, value);
	else
		return false;
}

bool SetCustomConfigOption_float(const char * name, const char * section, const char * key, float data)
{
	char value[65536] = "";
	sprintf_s(value, "%g", data);
	return SetCustomConfigOption(name, section, key, value);
}

UInt32 GetMaxArraySize()
{
	UInt32 maxArraySize = 0; // greater than 128 and up to $FFFFFFFF in theory (but don't, seriously). 0 means no change from vanilla
	GetCustomConfigOption_UInt32(pluginCustomIni, "Custom Arrays", "uMaxArraySize", &maxArraySize);
	return maxArraySize;
}



/**** papyrus functions ****/

// MFG morph data is stored in a float array at offset 0x3C8 in Actor::PROCESSTYPE::Data08
// morph values are stored starting at index 0x3C
// there are 0x36 morph values
float *GetMorphData(Actor *actor)
{
	if (!actor) {
		//_MESSAGE("Get Morph Data - No Actor.");
		return nullptr;
	}

	auto middleProcess = actor->middleProcess;
	if (!middleProcess) {
		//_MESSAGE("Get Morph Data - No Middle Process.");
		return nullptr;
	}

	auto data08 = middleProcess->unk08;
	if (!data08) {
		//_MESSAGE("Get Morph Data - No unk08.");
		return nullptr;
	}

	//NOTE: Off by 1 for the VR version.  For the other one it would be 0x3D0 >> 3.
	float* unk3C8 = (float*)data08->unk3B0[4];
	//float* unk3C8 = (float*)data08->unk3B0[3];
	if (!unk3C8) {
		//_MESSAGE("Get Morph Data - No unk3C8.");
		return nullptr;
	}

	return &unk3C8[0x3C];
}

//v24 = (float)v26 * 0.0099999998;
//if (v25 < 0x36 && v24 >= 0.0 && v24 <= 1.0)
//	*(float *)&v16[v25 + 0x3C] = v24;
bool MfgMorph_internal(Actor* actor, SInt32 morphID, SInt32 intensity)
{
	float *morphData = GetMorphData(actor);
	if (morphData)
	{
		float  fIntensity = (float)intensity * 0.0099999998;
		if (morphID > 0 && morphID < 0x36 && fIntensity >= 0.0 && fIntensity <= 1.0)
		{
			morphData[morphID] = fIntensity;
		}
		return true;
	}
	return false;
}

bool MfgMorph(StaticFunctionTag* base, Actor* actor, SInt32 morphID, SInt32 intensity)
{
	return MfgMorph_internal(actor, morphID, intensity);
}

bool MfgResetMorphs(StaticFunctionTag* base, Actor* actor)
{
	float *morphData = GetMorphData(actor);
	if (morphData)
	{
		for (int i = 1; i < 0x36; i++)
			morphData[i] = 0.0;	
		return true;
	}
	return false;
}

VMArray<float> MfgSaveMorphs(StaticFunctionTag* base, Actor* actor)
{
	VMArray<float> result;
	float *morphData = GetMorphData(actor);
	if (morphData)
	{
		for (int i = 0; i < 0x36; i++)
			result.Push(&morphData[i]);
		return result;
	}
	result.SetNone(true);
	return result;
}

bool MfgRestoreMorphs(StaticFunctionTag* base, Actor* actor, VMArray<float> values)
{
	float *morphData = GetMorphData(actor);
	if ((morphData) && (values.Length() == 0x36))
	{
		for (int i = 1; i < 0x36; i++)
			values.Get(&morphData[i], i);
		return true;
	}
	return false;
}

bool MfgApplyMorphSet(StaticFunctionTag* base, Actor* actor, VMArray<SInt32> morphIDs, VMArray<SInt32> values)
{
	float *morphData = GetMorphData(actor);
	if (!morphData) 
		return false;

	if (morphIDs.Length() != values.Length()) {
		_MESSAGE("Apply Morphs - Problem with morph IDs length.");
		return false;
	}

	SInt32 id;
	SInt32 val;
	for (int i = 0; i < values.Length(); i++)
	{
		morphIDs.Get(&id, i);
		values.Get(&val, i);
		float  fIntensity = (float)val * 0.0099999998;
		if (id > 0 && id < 0x36 && fIntensity >= 0.0 && fIntensity <= 1.0)
			morphData[id] = fIntensity;
		else {
			_MESSAGE("Apply Morphs - Problem with id or intensity.");
			return false;
		}
	}

	_MESSAGE("Apply Morphs - Done...");
	return true;
}


BSFixedString IntToHexString(StaticFunctionTag* base, SInt32 iNum)
{
	char tmpstr[20];
	_itoa_s(iNum, tmpstr, 20, 16);
	return BSFixedString(tmpstr);
}

VMArray<BSFixedString> StringSplit(StaticFunctionTag* thisInput, BSFixedString theString, BSFixedString theDelimiter)
{
	VMArray<BSFixedString> result;

	std::string str(theString);
	std::string delimiters(theDelimiter);

	//std::string::size_type whitespacePos = str.find_first_of(" ");

	//while (whitespacePos != std::string::npos) //eliminate whitespace. Taggers should tag with tags that have word starting with a capital letter. EG: "InsideOut", "TheQuickBrownFox"
	//{
	//	str.erase(whitespacePos, 1);
	//	whitespacePos = str.find_first_of(" ");
	//}

	std::string f(str); // Eliminate case sensitivity during find
	std::transform(f.begin(), f.end(), f.begin(), toupper);
	std::transform(delimiters.begin(), delimiters.end(), delimiters.begin(), toupper);

	std::string::size_type lastPos = f.find_first_not_of(delimiters, 0);
	std::string::size_type pos = f.find_first_of(delimiters, lastPos);

	while (std::string::npos != pos || std::string::npos != lastPos)
	{
		std::string token = str.substr(lastPos, pos - lastPos); // Pull from original string
		BSFixedString ConvToken = token.c_str(); //convert token into c string for BSFixedString
		result.Push(&ConvToken);
		lastPos = f.find_first_not_of(delimiters, pos);
		pos = f.find_first_of(delimiters, lastPos);
	}

	return result;
}


class Scaleform_MfgMorph : public GFxFunctionHandler
{
public:
	virtual void Invoke(Args * args)
	{
		UInt32 FormID = args->args[0].GetInt();
		UInt32 MorphID = args->args[1].GetInt();
		UInt32 Intensity = args->args[2].GetInt();
		TESForm * form = LookupFormByID(FormID);
		if (form)
		{
			Actor* actor = (Actor*)DYNAMIC_CAST(form, TESForm, Actor);
			MfgMorph_internal(actor, MorphID, Intensity);
		}
	}
};

bool RegisterScaleformFuncs(GFxValue * obj, GFxMovieRoot *movieRoot)
{
	RegisterFunction<Scaleform_MfgMorph>(obj, movieRoot, "MfgMorph");
	_MESSAGE("After MfgMorph register.");

	return true;
}

bool RegisterScaleform(GFxMovieView * view, GFxValue * plugin)
{
	GFxMovieRoot	* movieRoot = view->movieRoot;

	GFxValue currentSWFPath;
	const char* currentSWFPathString = nullptr;

	if (movieRoot->GetVariable(&currentSWFPath, "root.loaderInfo.url")) {
		currentSWFPathString = currentSWFPath.GetString();
	}
	else {
		_MESSAGE("WARNING: Scaleform registration failed.");
	}

	// Look for the menu that we want to inject into.
	if (strcmp(currentSWFPathString, "Interface/HUDMenu.swf") == 0) {
		GFxValue root; movieRoot->GetVariable(&root, "root");

		// Register native code object
		GFxValue obj; movieRoot->CreateObject(&obj);
		root.SetMember(PLUGIN_MENU, &obj);
		RegisterScaleformFuncs(&obj, movieRoot);
		
		_MESSAGE("Injected %s into Interface/HUDMenu.swf", PLUGIN_MENU);
	}
	return true;
}


bool RegisterFuncs(VirtualMachine* vm)
{
	_MESSAGE("RegisterFuncs");
	vm->RegisterFunction(
		new NativeFunction3<StaticFunctionTag, bool, Actor*, SInt32, SInt32>("MfgMorph", pluginName, MfgMorph, vm));
	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, bool, Actor*>("MfgResetMorphs", pluginName, MfgResetMorphs, vm));
	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, VMArray<float>, Actor*>("MfgSaveMorphs", pluginName, MfgSaveMorphs, vm));
	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, bool, Actor*, VMArray<float>>("MfgRestoreMorphs", pluginName, MfgRestoreMorphs, vm));
	vm->RegisterFunction(
		new NativeFunction3<StaticFunctionTag, bool, Actor*, VMArray<SInt32>, VMArray<SInt32>>("MfgApplyMorphSet", pluginName, MfgApplyMorphSet, vm));

	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, BSFixedString, SInt32>("IntToHexString", pluginName, IntToHexString, vm));
	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, VMArray<BSFixedString>, BSFixedString, BSFixedString>("StringSplit", pluginName, StringSplit, vm));

	return true;
}


extern "C"
{

bool F4SEPlugin_Query(const F4SEInterface * f4se, PluginInfo * info)
{

	OpenPluginLog();

	_MESSAGE("%s querying...", pluginName);

	// populate info structure
	if (info) {
		info->infoVersion =	PluginInfo::kInfoVersion;
		info->name =		pluginName;
		info->version =		pluginVersion;
	}

	// store plugin handle so we can identify ourselves later
	g_pluginHandle = f4se->GetPluginHandle();
	if (g_pluginHandle == kPluginHandle_Invalid) {
		_MESSAGE("problem getting plugin handle");
		return false;
	}

	if(f4se->isEditor)
	{
		_MESSAGE("loaded in editor, marking as incompatible");

		return false;
	}
	/*else if(f4se->runtimeVersion != REQUIRED_RUNTIME)
	{
		_MESSAGE("unsupported runtime version %08X (expected %08X)", f4se->runtimeVersion, REQUIRED_RUNTIME);

		return false;
	}*/

	// get the papyrus interface and query its version
	g_papyrus = (F4SEPapyrusInterface *)f4se->QueryInterface(kInterface_Papyrus);
	if(!g_papyrus)
	{
		_MESSAGE("couldn't get papyrus interface");

		return false;
	}

	/*if(g_papyrus->interfaceVersion < F4SEPapyrusInterface::kInterfaceVersion)
	{
		_MESSAGE("papyrus interface too old (%d expected %d)", g_papyrus->interfaceVersion, F4SEPapyrusInterface::kInterfaceVersion);

		return false;
	}*/

	// get the scale form interface and query its version
	g_scaleform = (F4SEScaleformInterface *)f4se->QueryInterface(kInterface_Scaleform);
	if (!g_scaleform)
	{
		_MESSAGE("couldn't get scale form interface");

		return false;
	}

	// get the messaging interface and query its version
	g_messaging = (F4SEMessagingInterface *)f4se->QueryInterface(kInterface_Messaging);
	if(!g_messaging)
	{
		_MESSAGE("couldn't get papyrus interface");

		return false;
	}

	/*if(g_messaging->interfaceVersion < F4SEMessagingInterface::kInterfaceVersion)
	{
		_MESSAGE("messaging interface too old (%d expected %d)", g_messaging->interfaceVersion, F4SEMessagingInterface::kInterfaceVersion);

		return false;
	}*/

	// ### do not do anything else in this callback
	// ### only fill out PluginInfo and return true/false

	// supported runtime version
	_MESSAGE("%s query successful.", pluginName);
	return true;
}

bool F4SEPlugin_Load(const F4SEInterface * f4se)
{
	bool queryResult = F4SEPlugin_Query(f4se, NULL);

	if (!queryResult) {
		_MESSAGE("Failed first load check.");
		return false;
	}

	_MESSAGE("%s loading...", pluginName);

	// apply patches to the game here
	strcpy_s(pluginCustomIni, pluginName);
	strcat_s(pluginCustomIni, ".ini");

	// register papyrus functions
	g_papyrus->Register(RegisterFuncs);

	// register plugin with scale form
	g_scaleform->Register(PLUGIN_MENU, RegisterScaleform);

	_MESSAGE("%s load successful.", pluginName);
	return true;
}

};
