#include <string>
#include <iostream>
#include <vector>
#include <utility>

using std::cout;
using std::endl;
using std::string;
using std::vector;

namespace {
	enum Colors { Blue, Lemon, Pink, Black};

	struct Outfit {
		Colors hair;
		Colors Tshirt;
	};

	class Miku final {
	private:
		vector <string> *songs;
		struct Outfit *outfit;

	public:
		Miku(vector<string>* songs_, Outfit* outfit_):songs(songs_), outfit(outfit_) {}

		~Miku() {
			delete songs;
			delete outfit;
		}

		Miku(Miku& rhs) {
			this->songs = new vector<string>(*rhs.songs);
			this->outfit = new Outfit(*rhs.outfit);
		}

		Miku& operator=(Miku& rhs) {
			if (&rhs != this) {
				delete songs;
				delete outfit;

				this->songs = new vector<string>(*rhs.songs);
				this->outfit = new Outfit(*rhs.outfit);
			}
			return *this;
		}

		Miku(Miku&& rhs) {
			std::swap(this->songs, rhs.songs);
			std::swap(this->outfit, rhs.outfit);
		}

		Miku& operator=(Miku&& rhs) {
			if (&rhs != this) {
				std::swap(songs, rhs.songs);
				std::swap(outfit, rhs.outfit);
			}
			return *this;
		}

		void SayHello() {
			cout << "Hello, welcome to the concert!" << endl;
		}

		void ListRepertoire() {
			auto end = songs->end();
			for (auto start{ songs->begin() }; start != end; start++) {
				cout << *start << " ";
			}
			puts(".");
		}
	};
}

int main() {

	vector<string>* concert1Songs = new vector<string>({ "World is Mine", "Milgram"});
	Outfit *concert1Clothes= new Outfit{ Blue, Blue };
	Miku* miku = new Miku{ concert1Songs, concert1Clothes };

	Miku* mikuCopy = new Miku(*miku);

	delete miku;

	Miku* mikuMoved = new Miku(std::move(*mikuCopy));

	delete mikuCopy;

	mikuMoved->SayHello();
	mikuMoved->ListRepertoire();

	delete mikuMoved;
	return 0;
}
