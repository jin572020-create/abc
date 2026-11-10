int main() {
    {
        int inner = 1;
        {
            int nested = inner + 2;
            inner = nested;
        }
    }
    return 0;
}
