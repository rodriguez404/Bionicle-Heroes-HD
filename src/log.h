#pragma once

// Plain-text log next to the .asi (BetterBionicleHeroes.log), flushed per line.
void LogOpen(const char* path);
void Log(const char* fmt, ...);
