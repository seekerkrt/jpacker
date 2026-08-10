#pragma once

struct AppConfig;
struct ParsedCliArguments;

// Production pre-mutation authorityだけを観測し、actual capabilityを保持しない。
int run_dry_run(
        const ParsedCliArguments& parsed,
        const AppConfig& config);
