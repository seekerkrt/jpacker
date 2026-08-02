#include <sys/types.h>

// POLICY: production objectsを変更せず、root guardの実geteuid callだけを
// dedicated test linkでUID 0へ固定する。
extern "C" uid_t __wrap_geteuid() {
    return 0;
}
