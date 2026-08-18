#define LITODOC_DECLARE(name) int name() { return 0; }

LITODOC_DECLARE(generated)

namespace {
struct Hidden {};
}

struct {
    int value;
} anonymous_object;
