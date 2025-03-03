/*!
 * tf2patcher.c
 * TF2 decal tool patcher
 * (c) default-username 2020
 */

#include "base.h"
#include "common.h"
#include "memory.h"


// platform-dependent code
// TODO implement these for linux
#ifdef WINDOWS

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char window_title[MAX_PATH];
    GetWindowTextA(hwnd, window_title, sizeof(window_title));

    if (strncmp(window_title, "Team Fortress 2", 15) == 0) {
        *((HWND *)lParam) = hwnd; // Set the found window handle
        return FALSE; // Stop enumeration
    }

    return TRUE; // Continue enumeration
}

// acquire hwnd to tf2 window
// wait if the window is not there yet
HWND get_tf2_window(void) {
    HWND window = NULL;

    // Use EnumWindows to find the TF2 window
    if (!EnumWindows(EnumWindowsProc, (LPARAM)&window)) {
        printf("Waiting for TF2 to start...\n");

        do {
            Sleep(1000);
            EnumWindows(EnumWindowsProc, (LPARAM)&window);
        } while (!window);
    }

    verbose_print("Found TF2 window with HWND %u\n", window);
    return window;
}

    // find client.dll in tf2 module table
    // wait for max 15 seconds if it's not yet loaded
    HMODULE get_tf2_client_module(HANDLE process)
    {
        HMODULE modules[8192];
        DWORD num_modules;
        char module_name[MAX_PATH], tempbuf[MAX_PATH], error_buf[8192];
        size_t num_retry = 0;

        do {
            if (!EnumProcessModulesEx(process, modules, sizeof(modules), &num_modules, LIST_MODULES_ALL)) {
                fprintf(stderr, "Cannot enumerate TF2 modules: %s\n", describe_error(error_buf, sizeof(error_buf)));
                return 0;
            } else {
                assert(num_modules % sizeof(HMODULE) == 0);
                num_modules /= sizeof(HMODULE);

                verbose_print("Found %u modules in TF2 process\n", num_modules);

                for (size_t i = 0; i < num_modules; i++) {
                    if (GetModuleFileNameExA(process, modules[i], module_name, sizeof(module_name)) &&
                            !strcmp(extract_file_name(module_name, tempbuf, sizeof(tempbuf)), "client.dll")) {
                        verbose_print("Found TF2 client.dll (%zu/%u)\n", i + 1, num_modules);
                        return modules[i];
                    }
                }
            }

            Sleep(1000);
        } while (num_retry++ <= 30);

        fprintf(stderr, "Failed to find TF2 client.dll module!\n");
        return 0;
    }


    bool attach_to_tf2(void)
    {
        char error_buf[8192];

        HWND window = get_tf2_window();

        DWORD process_id;
        GetWindowThreadProcessId(window, &process_id);
        HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);

        if (!process) {
            fprintf(stderr, "Cannot attach to TF2 process: %s\n", describe_error(error_buf, sizeof(error_buf)));
        } else {
            verbose_print("Attached to TF2 process with PID %u\n", process_id);

            HMODULE module = get_tf2_client_module(process);
            if (module) {
                pinfo.process = process;
                pinfo.module = module;

                return true;
            }

            CloseHandle(process);
        }

        return false;
    }


    bool calc_client_module_bounds(void)
    {
        pinfo.cl_base = (unsigned char *)pinfo.module;

        IMAGE_DOS_HEADER dos_hdr;
        IMAGE_NT_HEADERS nt_hdr;

        if (read_mem(pinfo.cl_base, &dos_hdr, sizeof(dos_hdr)) &&
            read_mem(pinfo.cl_base + dos_hdr.e_lfanew, &nt_hdr, sizeof(nt_hdr))) {

            pinfo.cl_size = nt_hdr.OptionalHeader.SizeOfImage;
            verbose_print("TF2 client.dll module: 0x%" PRIXPTR " with sz=%zu\n",
                    (uintptr_t)pinfo.cl_base, pinfo.cl_size);
            return true;
        }

        fprintf(stderr, "Failed to calculate TF2 client.dll module size!\n");
        return false;
    }


    void free_resources(void)
    {
        if (pinfo.process) {
            CloseHandle(pinfo.process);
            pinfo.process = 0;
        }
    }

#endif

// perform new patching method thanks to leaked tf2 source code
bool do_patch(void)
{
    printf("Patching...\n");

    // CConfirmCustomizeTextureDialog::PerformFilter
    unsigned char pattern[] = {
		0x09, 0x83, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xC7
    };

    unsigned char *addr = find_mem_cl(pattern, sizeof(pattern));
    if (!addr) {
        fprintf(stderr, "Failed to find pattern in client library!\n");
    } else {
        verbose_print("Pattern addr: 0x%" PRIXPTR "\n", (uintptr_t)addr);

        set_mem(addr, (unsigned char []){0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xFF, 0xC7, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}, 15);
        verbose_print("Patched!\n");

		// and thats pretty much it
		printf("Done!\n");
		return true;
    }

    return false;
}




int main(int argc, char *argv[])
{
    printf(
        "  --------------------------------------------\n"
        "  |      TF2 equip region patcher 1.0.0      |\n"
        "  --------------------------------------------\n"
        "\n");

    if (argv) {
        for (int i = 0; i < argc; i++) {
            if (argv[i]) {
                if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                    printf(
                        "Usage: tf2patcher -[hv]\n"
                        "       -h | --help    : show this help\n"
                        "       -v | --verbose : be more verbose\n");
                    return EXIT_SUCCESS;
                } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
                    pinfo.verbose_mode = true;
                }
            }
        }
    }

    if (pinfo.verbose_mode) {
        printf("Running in verbose mode\n");
    }

    bool res = attach_to_tf2() && calc_client_module_bounds() && do_patch();

    free_resources();
    if (!res) {
        printf("Press any key to exit...\n");
        (void)(getchar());
    }

    return res ? EXIT_SUCCESS : EXIT_FAILURE;
}
