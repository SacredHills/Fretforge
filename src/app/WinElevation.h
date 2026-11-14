#pragma once
namespace ff
{
/** True when the process runs elevated ("as administrator") on Windows; always false elsewhere. */
bool isRunningElevated();
}
