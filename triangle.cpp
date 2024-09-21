#include <iostream>

using std::cout;
using std::endl;

namespace {
    template <typename T>
    class Point{
    public:
        T x; T y;
    };

    template <typename U> struct Triange{
    public:
        struct Point<U> pts [3];
        U double_square(){
            U sq = pts[0].x * (pts[1].y - pts[2].y) +
                        pts[1].x * (pts[2].y - pts[0].y) +
                        pts[2].x * (pts[0].y - pts[1].y);
            return (sq > 0) ? sq : -sq;
        }
    };
}

int main(){
    Triange<int> triange;
    triange.pts[0] = Point<int>(1.0, 1.0);
    triange.pts[1] = Point<int>(2.0, 4.0);
    triange.pts[2] = Point<int>(6.0, 1.0);

    double square = triange.double_square();

    cout << square/2 << endl;
    return 0;
}

/*practice trash:
#include <iostream>
#include <vector>
#include <concepts>
#include <unordered_map>
#include <random>
#include <type_traits>

template <typename T>
concept large_size = sizeof(T) > sizeof(int);

template <typename T>
concept small_size = sizeof(T) < sizeof(int);


template <typename T>
concept size = small_size<T> || large_size<T>;


constexpr void print_all() {
	return;
}

template<typename... Args>
constexpr void print_all(long fst, Args... args) {
	std::cout << fst << "l" << " ";
	print_all(args...);
}

template<typename T, typename... Args>
constexpr void print_all(T fst, Args... args) {
	std::cout << fst << " ";
	print_all(args...);
}


template <typename T> 
concept Addadle = requires(T && fst, T && snd) {
	{ fst + snd } -> std::same_as<T>;
};


template <typename Iter>
concept Iterator = requires (Iter it) {
	{ *it };
	{ ++it };
	{ --it };
	{ it != it };
};


template<Iterator Iter>
void print_range(Iter begin, Iter end) {
	for (Iter it = begin; it != end; ++it) {
		std::cout << *it << ' ';
	}
	std::cout << '\n';
}


template <typename T>
T Add(T&& lhs, T&& snd) requires Addadle<T> {
	return lhs + snd;
}
	
template <typename T>
concept is_summing = requires(T fst, T snd) {
	{ fst + snd };
};






template <typename T>
class has_invoke {
private:
	template <typename U>
	static auto test(U*) -> decltype(std::declval<U>().Invoke(), std::true_type());

	template <typename>
	static std::false_type test(...);

public:
	static constexpr bool value = decltype(test<T>(nullptr))::value;
};

template <typename T>
concept is_invoke = requires(T type) {
	{ type.Invoke() };
};

template<is_invoke T>
auto Invoke(T& elem) {
	elem.Invoke();
}

template <typename T>
typename std::enable_if<has_invoke<T>::value>::type
CallInvoke(T& elem) {
	elem.Invoke();
}

class Hope {
public:
	int Invoke() const {  return 42; }
};

std::ostream& operator<<(std::ostream& os, const Hope* h)
{
	os << "Res is " << h-> Invoke();
	return os;
}

int  main() {
	Hope* robin = new Hope{};
	Invoke(*robin); 
	CallInvoke(*robin);
	Hope Robin = Hope();
	std::cout << robin;
}
*/
