int helper(int value) {
    const int LIMIT = 4;
    if (value < LIMIT) {
        return value + 1;
    } else {
        return value;
    }
}
void noop() { return; }
int main() {
    const int LIMIT = 4;
    int value = helper(0);
    while (value < LIMIT) {
        value = helper(value);
    }
    return value;
}
