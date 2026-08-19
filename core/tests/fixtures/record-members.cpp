template <typename T>
concept Numeric = requires(T value) { value + value; };

template <typename T>
class Box {
public:
    /// Stored value.
    [[no_unique_address]] T value{};

    /// Small flags.
    unsigned flags : 3 = 1;

    Box() = default;
    ~Box() = default;

    /// Returns the stored value.
    [[nodiscard]] constexpr auto get(int offset = 0) const & noexcept -> T;

    template <Numeric U>
    auto convert(U input = U{}) && noexcept(false) -> U;

    explicit operator bool() const noexcept;
};

auto make_box() -> Box<int>;
