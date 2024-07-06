#include "Instrumentor.hpp"

Instrumentor::Instrumentor() : _CurSession(nullptr), _ProfileCount(0) {}

Instrumentor::~Instrumentor() {}

void Instrumentor::BeginSession(const std::string& name, const std::string& path) {
    _OutputStream.open(path);
    WriteHeader();
    _CurSession = new InstrumentationSession{ name };
}

void Instrumentor::FinishSession() {
    WriteFooter();
    _OutputStream.close();
    delete _CurSession;
    _CurSession = nullptr;
    _ProfileCount = 0;
}

void Instrumentor::WriteProfile(const ProfileResult& res) {
    if (_ProfileCount++ > 0)
        _OutputStream << ",";

    std::string name = res.Name;
    std::replace(name.begin(), name.end(), '"', '\'');

    _OutputStream << "{";
    _OutputStream << "\"cat\":\"function\",";
    _OutputStream << "\"dur\":" << (res.End - res.Start) << ',';
    _OutputStream << "\"name\":\"" << name << "\",";
    _OutputStream << "\"ph\":\"X\",";
    _OutputStream << "\"pid\":0,";
    _OutputStream << "\"tid\":" << res.ThreadID << ",";
    _OutputStream << "\"ts\":" << res.Start;
    _OutputStream << "}";

    _OutputStream.flush();
}

void Instrumentor::WriteHeader() {
    _OutputStream << "{\"otherData\": {},\"traceEvents\":[";
    _OutputStream.flush();
}

void Instrumentor::WriteFooter() {
    _OutputStream << "]}";
    _OutputStream.flush();
}

Instrumentor& Instrumentor::Get() {
    static Instrumentor instance;
    return instance;
}

InstrumentationTimer::InstrumentationTimer(const char* name)
    : m_Name(name), m_Stopped(false) {
    m_StartTimepoint = std::chrono::high_resolution_clock::now();
}

InstrumentationTimer::~InstrumentationTimer() {
    if (!m_Stopped)
        Stop();
}

void InstrumentationTimer::Stop() {
    auto endTimepoint = std::chrono::high_resolution_clock::now();

    long long start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch().count();
    long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();

    uint32_t threadID = std::hash<std::thread::id>{}(std::this_thread::get_id());
    Instrumentor::Get().WriteProfile({ m_Name, start, end, threadID });

    m_Stopped = true;
}
