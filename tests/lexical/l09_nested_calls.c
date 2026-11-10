int f(int a) { return a; }
int g(int a, int b) { return a + b; }
int main() {
    int x = g(f(1), g(2, f(3)));
    return x;
}
