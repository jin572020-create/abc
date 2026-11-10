int add(int a, int b) {
    return a + b;
}
int main() {
    int x = add(1, 2);
    ;
    { int y = x; }
    return 0;
}
