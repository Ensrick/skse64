#include "LoaderError.h"
#include <cstdarg>

void PrintLoaderError(const char * fmt, ...)
{
	char	buf[4096];

	va_list	args;

	va_start(args, fmt);
	gLog.Log(IDebugLog::kLevel_FatalError, fmt, args);
	va_end(args);

	va_start(args, fmt);
	vsprintf_s(buf, sizeof(buf), fmt, args);
	va_end(args);

	// headless doctrine: errors are already in skse64_loader.log via gLog above;
	// a modal box blocks unattended launch chains, so never show one
	(void)buf;
}
