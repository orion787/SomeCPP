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
