# AF_XDP scatter/gather verification

Checked 2026-11-10 against upstream Linux v6.12, the plan's baseline, with a separate inspection of the retrieved upstream master mlx5 Tx file. This is source/API verification, not a physical NIC transmission test. The available host reports the ena driver; it does not provide an ice/mlx5 hardware test result.

## Current design disposition

The adopted TLS path uses fixed MTU-sized registered packet backing. Providers publish size/headRoom/tailRoom before Zi constructs the adapter. Each connection passes its own mps payload cap to allocBuf(owner, length, mps); shared provider geometry and count remain fixed. TxTLS plaintext and RxTLS plaintext use ordinary builtin/VHeap storage. The separate [Ztls migration](../ztls_fix.md) encrypts serially into reserved TxTCP packet buffers. This supersedes the earlier plugin.max proposal and the large registered-buffer/header-table/SG requirement.

Each complete packet uses one descriptor without XDP_USE_SG. The TLS-specific AF_XDP multi-buffer requirement is removed. Actual zero-copy selection remains a kernel/driver decision: inspect XDP_OPTIONS after bind. The veth and physical mlx5 transmission gates remain pending; neither the selected layout nor the historical source inspection proves hardware zero-copy operation. The findings below retain the earlier SG layout and its driver limitations as historical evidence.

## Historical SG result

The proposed header-table plus contiguous TLS ciphertext layout is representable by AF_XDP. Each Ethernet packet is a separate XDP_PKT_CONTD chain of header and payload fragments; one roughly 16 KiB TLS record produces multiple MTU-bounded packets. This is explicit software TCP segmentation, not TSO. Ring batching alone does not provide scatter/gather.

Hardware zero-copy for that layout requires AF_XDP multi-buffer support in the actual driver/device, not merely ordinary socket scatter/gather, XDP scatter/gather, or single-buffer AF_XDP zero-copy. All vector entries are UMEM offsets/lengths. Linux aligned Tx validation accepts interior offsets provided each descriptor stays within one chunk. Several packet chains can therefore reference disjoint ranges of one retained logical allocation. The existing reference-count and in-flight immutability requirements still apply.

## Inspected implementations

| Source | Evidence | Consequence |
| --- | --- | --- |
| Linux v6.12 ice | ice_main.c sets xdp_zc_max_segs = ICE_MAX_BUF_TXD; ice_txrx.h defines that as 8. ice_xmit_zc consumes descriptor batches, and ice_xmit_pkt/ice_xmit_pkt_batch obtain DMA addresses for each UMEM range and derive the NIC end-of-packet bit from xsk_is_eop_desc. | A concrete hardware-driver implementation of zero-copy per-packet vectors exists. Check effective device capability and actual geometry rather than hardcoding 8 globally. |
| Linux v6.12 mlx5 | mlx5e_xsk_tx consumes one descriptor per transmit via xsk_tx_peek_desc, without assembling XDP_PKT_CONTD chains. It does not override the netdevice default xdp_zc_max_segs = 1. The separately retrieved upstream master en/xsk/tx.c still uses that single-descriptor path; this is not a promise about vendor forks or future releases. | Ordinary mlx5 XDP multi-buffer features do not establish AF_XDP zero-copy multi-buffer Tx support. Do not claim this layout achieves hardware zero-copy on that inspected driver. |
| Linux v6.12 xsk_buff_pool.c | xp_assign_dev rejects the zero-copy attempt when XDP_USE_SG is set and xdp_zc_max_segs == 1; without forced XDP_ZEROCOPY it permits kernel copy fallback. | A successful ordinary bind with XDP_USE_SG is not proof of hardware zero-copy. This can fall back even where single-buffer AF_XDP zero-copy works. |

## Historical SG implementation checks

Query generic netlink netdev capabilities: NETDEV_XDP_ACT_XSK_ZEROCOPY and NETDEV_A_DEV_XDP_ZC_MAX_SEGS. A maximum of 1 means no zero-copy multi-buffer support. Query XDP_OPTIONS after bind to record actual zero-copy selection. Keep the adopted ordinary-bind policy: do not set XDP_COPY or force XDP_ZEROCOPY. Capability reporting describes the selected operation; it does not introduce a hardware certification gate or forbid kernel-selected copying.

Count the complete descriptor chain of each Ethernet packet, including header/payload chunk-boundary splits and any padding. Validate it against the effective mode's fragment limit before publication; a 2-entry header/payload vector may need more entries after chunk splitting. Never publish an unsupported chain or interpret a completion as successful transmission. Test the exact header-table/noncontiguous vector with boundary cases and a full TLS-sized logical buffer in the independent Zxdp gate. Veth verifies packet/vector semantics in software; physical-device testing is needed to demonstrate actual hardware zero-copy.

The historical header-table layout depended on multi-buffer-capable AF_XDP for hardware zero-copy. That dependency does not apply to the subsequently adopted single-buffer packet path. The source findings above still explain why enabling XDP_USE_SG would obstruct the inspected mlx5 zero-copy path.

## Primary sources

- [AF_XDP multi-buffer API and probing](https://docs.kernel.org/networking/af_xdp.html#multi-buffer-support)
- [v6.12 descriptor validation](https://github.com/torvalds/linux/blob/v6.12/net/xdp/xsk_queue.h)
- [v6.12 bind and copy-fallback checks](https://github.com/torvalds/linux/blob/v6.12/net/xdp/xsk_buff_pool.c)
- [v6.12 ice Tx](https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/intel/ice/ice_xsk.c)
- [v6.12 ice capabilities](https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/intel/ice/ice_main.c)
- [v6.12 ice fragment limit](https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/intel/ice/ice_txrx.h)
- [v6.12 mlx5 Tx](https://github.com/torvalds/linux/blob/v6.12/drivers/net/ethernet/mellanox/mlx5/core/en/xsk/tx.c)
- [v6.12 default netdevice segment limit](https://github.com/torvalds/linux/blob/v6.12/net/core/dev.c)
- [Retrieved upstream master mlx5 Tx](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/mellanox/mlx5/core/en/xsk/tx.c)
