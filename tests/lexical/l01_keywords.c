void f(int x) { return; }
int main() {
    const int limit = 10;
    int i = 0;
    while (i < limit) {
        if (i == 5) {
            break;
        } else {
            i = i + 1;
            continue;
        }
        i = i + 1;
    }
    return 0;
}
