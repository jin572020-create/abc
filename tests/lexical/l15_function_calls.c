int add(int left, int right) {
    return left + right;
}
int main() {
    int value = add(add(1, 2), add(3, 4));
    return value;
}
