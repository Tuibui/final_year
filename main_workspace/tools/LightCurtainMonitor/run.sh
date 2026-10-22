#!/usr/bin/env bash
set -euo pipefail

gui_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$gui_dir/../../.." && pwd)"

runtime_id=""
if [[ "$(uname -s)" == Linux ]]; then
    case "$(uname -m)" in
        aarch64|arm64) runtime_id="linux-arm64" ;;
        x86_64|amd64) runtime_id="linux-x64" ;;
    esac
fi

# Use the local bridge on Jetson unless an endpoint or config was supplied.
if [[ "$runtime_id" == linux-arm64 ]]; then
    has_connection_option=false
    for arg in "$@"; do
        case "$arg" in
            --endpoint|--config) has_connection_option=true ;;
        esac
    done
    if [[ "$has_connection_option" == false ]]; then
        set -- --endpoint ws://localhost:9090 "$@"
    fi
fi

if [[ -n "$runtime_id" ]]; then
    prepared_app="$repo_dir/runtime/gui/$runtime_id/LightCurtainMonitor"
    if [[ -x "$prepared_app" ]]; then
        exec "$prepared_app" "$@"
    fi
fi

export DOTNET_CLI_HOME="$repo_dir/runtime/tools/dotnet-home"
export NUGET_PACKAGES="$repo_dir/runtime/tools/nuget"
export DOTNET_CLI_TELEMETRY_OPTOUT=1

if command -v dotnet >/dev/null 2>&1; then
    dotnet_cmd="$(command -v dotnet)"
elif [[ -x "$repo_dir/runtime/tools/dotnet/dotnet" ]]; then
    dotnet_cmd="$repo_dir/runtime/tools/dotnet/dotnet"
else
    echo "No prepared GUI for this machine was found under $repo_dir/runtime/gui." >&2
    echo "Install the prepared build or a .NET 8 SDK. See $gui_dir/README.md" >&2
    exit 1
fi

exec "$dotnet_cmd" run --project "$gui_dir/LightCurtainMonitor.csproj" -c Release -- "$@"
