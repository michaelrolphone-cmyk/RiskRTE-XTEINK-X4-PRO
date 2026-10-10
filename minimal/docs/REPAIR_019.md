# X4 Minimal 0.1.9 repair candidate

This UC8279 test build combines the reported cold-start RTC, Quick Controls,
partial-refresh, startup-logo and Home Points corrections. It is assembled from
clean, explicitly recorded source commits; its build receipt distinguishes the
native Runtime source from each app and provider source.

- Cold Clock accepts absent timezone/RTC-basis preferences using the existing
  checked defaults, reads the RTC, releases it, then seeds native time before the
  alarm service is polled. Corrupt records and hardware failures remain errors.
- Quick Controls stays open across minute changes and long inspection. Rendering
  snapshots the state actually submitted, so changes received during a pending
  refresh remain dirty and the final screen converges to the latest brightness.
- Completed UC8279 frames establish usable image history. Subsequent partial
  damage can select the partial waveform; initial/explicit-clean frames and
  invalid history still use full refresh. File Browser now permits partial damage.
- Cold/reset first promotion displays the existing RiscRTE logo without a timed
  hold. Returning Home and retained minute/GPIO wakes do not repeat the splash.
- Home displays the actual Points schedule, custom labels, next time, countdown,
  progress and upcoming rows. Tapping the panel opens Points in Time. The extra
  namespace-5 read is foreground-only; retained minute wakes do not acquire it.
- Runtime 0.1.55 uses the existing committed-pair journal at boot. Install/update
  admission remains the validation gate; boot no longer rehashes the installed
  firmware and store.

The measured old committed-pair boot path performed 1,913 explicit reads totaling
7,828,416 bytes, 1,606 SHA updates over 6,575,920 bytes and 1,912 yields. The new path
performs one 96-byte journal read, with no hash or image/marker scans. Additional
hidden SDK verifier reads were excluded from the old count. This is deterministic
operation evidence, not a measured hardware speedup.

Touch is sampled while idle and during display transfers/BUSY. Ordinary app
input dispatch still waits for the current presentation to finish. The panel
continues sending two 60,000-byte controller planes; partial waveform selection
does not yet reduce that transfer. Paper pressed-state animations, clock/app
crossfades and animated sheet dragging are not implemented by this repair.

The image is a complete 16 MiB UC8279 layout, flashed at address 0x0. It replaces
firmware, installed apps and initial persistent partitions; preserve wanted
settings/data before flashing. USB serial uses 115200. Host, target and store
admission tests are recorded separately from physical qualification, which has
not been performed by the build task. Delivered 0.1.8 artifacts remain unchanged.
