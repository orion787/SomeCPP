#pragma once

#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>

struct InstrumentationSession {
    std::string Name;
};

struct ProfileResult {
    std::string Name;
    long long Start, End;
    uint32_t ThreadID;
};

class Instrumentor {
private:
    InstrumentationSession* _CurSession;
    std::ofstream _OutputStream;
    int _ProfileCount;

public:
    Instrumentor();
    ~Instrumentor();

    void BeginSession(const std::string& name, const std::string& path = "res.json");
    void FinishSession();
    void WriteProfile(const ProfileResult& res);

    static Instrumentor& Get();

private:
    void WriteHeader();
    void WriteFooter();
};

class InstrumentationTimer {
public:
    InstrumentationTimer(const char* name);
    ~InstrumentationTimer();

    void Stop();

private:
    const char* m_Name;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimepoint;
    bool m_Stopped;
};
