// ntctest: apps/babywarmer/ntc.cpp's ADS1115 code -> degrees C, at the
// interesting codes. Records the mapping as it is (picoos @ 89c16e5),
// including the wrap above code 21742 (see apps/babywarmer/ntc.h).
#include <cstdio>
#include <cstdint>
float ntc_convert(uint16_t adc_value);
int main()
{
    const int codes[] = { -32768, -11026, -11025, -11000, -5000, 0, 5000,
                          9743, 9744, 15000, 21742, 21743, 25000, 32767 };
    for (int c : codes)
        printf("code %6d  ->  %5.1f C\n", c, ntc_convert((uint16_t)(int16_t)c));
    // where does it run through 37 C, and is it monotonic over the table?
    float prev = 1e9; int bad = 0;
    for (int c = -11025; c <= 9743; c++) {
        float t = ntc_convert((uint16_t)(int16_t)c);
        if (t > prev) bad++;
        prev = t;
    }
    printf("monotonic (falling) over -11025..9743: %s\n", bad ? "NO" : "yes");
    return 0;
}
