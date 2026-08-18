export module litodoc.executable;

namespace lito::doc::tool {
auto run() -> int;
}

extern "C++" int main() { return lito::doc::tool::run(); }
