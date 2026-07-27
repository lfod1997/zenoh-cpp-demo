#include <zenoh.hxx>

int main() {
    const auto session = zenoh::Session::open(zenoh::Config::create_default());
    session.put(
        {"demo/example/simple"},
        {"Hello Zenoh!"}
    );
    return 0;
}
