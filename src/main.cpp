/* 
    BetterHomebrewBrowser, A homebrew browser for the PlayStation Vita with background downloading support
    Copyright (C) 2024 Muhammad Ibrahim

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.    
*/

#include <kernel.h>
#include <paf.h>
#include <shellsvc.h>
#include <libsysmodule.h>
#include <apputil.h>
#include <taihen.h>
#include <net.h>
#include <libperf.h>
#include <libnetctl.h>
#include <vshbridge.h>

#include "print.h"
#include "utils.h"
#include "common.h"
#include "bhbb_plugin.h"
#include "bhbb_locale.h"
#include "pages/app_browser.h"
#include "pages/text_page.h"
#include "db/source.h"
#include "settings.h"
#include "downloader.h"

#define NET_HEAP_SIZE  SCE_KERNEL_256KiB
#define HTTP_HEAP_SIZE SCE_KERNEL_256KiB
#define SSL_HEAP_SIZE  SCE_KERNEL_256KiB

extern "C" {
    const char			sceUserMainThreadName[] = "BHBB_MAIN";
    const int			sceUserMainThreadPriority = SCE_KERNEL_DEFAULT_PRIORITY_USER;
    const unsigned int	sceUserMainThreadStackSize = SCE_KERNEL_THREAD_STACK_SIZE_DEFAULT_USER_MAIN;

    int curl_global_memmanager_set_np(void *(*allocate)(size_t), void (*deallocate)(void *), void *(*reallocate)(void *, size_t));
}

using namespace paf;

Plugin *g_appPlugin = nullptr;

SceVoid PluginStart(Plugin *plugin)
{
    if(plugin == nullptr)
    {
        print("[bhbb_plugin] Error Plugin load failed!\n");
        return;
    }

    g_appPlugin = plugin;
    print("[bhbb_plugin] Create success! %p\n", plugin);

    // TLS may be provided by the system, iTLS-Enso, tls_fixer, vitals,
    // or another compatible plugin. Do not require one specific module.

    sceShellUtilInitEvents(0);
    
    job::JobQueue::Option opt;
    opt.workerNum = 1;
    opt.workerStackSize = SCE_KERNEL_256KiB;
    
    job::JobQueue::Init(&opt);

    SceNetInitParam netInitParam;
	netInitParam.memory = sce_paf_malloc(NET_HEAP_SIZE);
	netInitParam.size = NET_HEAP_SIZE;
	netInitParam.flags = 0;
	sceNetInit(&netInitParam);

	sceNetCtlInit();

    /* HTTPS */
	sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
	sceHttpInit(HTTP_HEAP_SIZE);
	sceSslInit(SSL_HEAP_SIZE);
    
    new Downloader();
    new Settings();

    auto page = new AppBrowser(Source::Create((Source::ID)Settings::GetInstance()->source));
    page->Load();
}

int main()
{
#if defined(SCE_PAF_TOOL_PRX) && defined(_DEBUG) && !defined(__INTELLISENSE__)
    //This line will break things if using non devkit libpaf
    SCE_PAF_AUTO_TEST_SET_EXTRA_TTY(sceIoOpen("tty0:", SCE_O_WRONLY, 0)); 
#endif

    Utils::StartBGDL(); // Init bhbb_dl
    Utils::InitMusic(); // BG Music

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER);
    sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_COMMON_GUI_DIALOG);

    new Module("app0:module/libcurl.suprx", "libcurl");
    curl_global_memmanager_set_np(sce_paf_malloc, sce_paf_free, sce_paf_realloc);

    Framework::InitParam fwParam;

    Framework::SampleInit(&fwParam);
    fwParam.mode = Framework::Mode_Application;
    
    fwParam.surface_pool_size = SCE_KERNEL_8MiB;
    fwParam.text_surface_pool_size = SCE_KERNEL_4MiB; 

    Framework *fw = new Framework(fwParam);

    fw->LoadCommonResourceSync();

    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;

    //Can use sce_paf_... because paf is preloaded
    sce_paf_memset(&init, 0, sizeof(SceAppUtilInitParam));
    sce_paf_memset(&boot, 0, sizeof(SceAppUtilBootParam));
    
    sceAppUtilInit(&init, &boot);

    Plugin::InitParam piParam;

    piParam.name = "bhbb_plugin";
    piParam.resource_file = "app0:resource/bhbb_plugin.rco";
    piParam.caller_name = "__main__";

#if defined(SCE_PAF_TOOL_PRX) && defined(_DEBUG)
    //This line will break things if using non devkit libpaf
    piParam.option = Plugin::Option_ResourceLoadWithDebugSymbol;
#endif

    piParam.start_func = PluginStart;

    Plugin::LoadAsync(piParam);
    
    fw->Run();

    return sceKernelExitProcess(0);
}