# RTC power before MSPI experiment

This X4 0.1.33 diagnostic candidate adapts the order-only startup stability fix from Espressif commit 5b71b949be35ae2577bf8989febc120df2520037 (https://github.com/espressif/esp-idf/commit/5b71b949be35ae2577bf8989febc120df2520037) to the pinned IDF4.4.7 SDK. Battery-only reset behavior remains unverified.

The existing PLL recalibration and RTC_CONFIG_DEFAULT/power-on OCode initialization run before flash/PSRAM timing training, once. Later CPU/RTC clock selection and watchdog initialization retain their original SDK function body. Clocks, GPIO configuration, brownout threshold, bootloader and partitions are unchanged. The selected hook executes after flash mapping/cache is established; this is not a pre-cache or ROM hook. There is no new GPIO, storage, allocation, scheduler or diagnostic I/O in the hook.

The exact target proof decodes all four wrapper call sites and their ordering, original SDK call sequences, IRAM/DRAM placement, and compares bootloader and partition bytes. The host test uses the original IDF clock function body and five reset classes. Run both for every final candidate. This is an explicit experiment, not a claim that the upstream defect caused the reported hardware behavior.

The logging/USB/app cohort is preserved from .32, including its currently known SD export refusal issue. The separate transaction-safe USB preparation repair is in progress and is not included here.
