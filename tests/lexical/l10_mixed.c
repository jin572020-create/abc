/* ToyC lexer stress case: every terminal class in one file. */
int helper(int p1, int p2) {
    int local = p1 * 2 - p2 / 3 % 4;
    while (local != 0 && p1 < p2 || !(p1 >= p2)) {
        local = local - 1;
        if (local == 0) { break; } else { continue; }
    }
    return local;
}
void nothing() { return; }
int main() {
    const int K = 42;
    int r = helper(K, 7);
    return r;
}
