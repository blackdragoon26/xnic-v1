# Gated ENA and EFA validation

This directory prepares the two cloud experiments that follow physical W5500
bring-up. It does not provision anything automatically.

Current status on 2026-08-08: **blocked before preflight**. The configured
region is `eu-north-1`, but `aws sts get-caller-identity` returned
`InvalidClientTokenId`. No cloud resource was created and no cost was incurred.

Run the read-only preflight after repairing credentials:

```sh
AWS_REGION=eu-north-1 ./cloud/scripts/aws-preflight.sh
```

It verifies identity, lists available zones and EFA-capable instance types, and
shows relevant EC2 on-demand quotas. It cannot launch, modify, or terminate a
resource.

## Mandatory cost gate

Before any instance is launched, write down:

- exact instance type and architecture
- region and one shared Availability Zone
- current on-demand hourly price per instance
- number of instances
- storage and public-address costs
- hard runtime cap
- maximum total spend
- termination command and independent console check

No launch is authorized merely because preflight passes.

## Real ENA DPDK gate

Use an instance with a management interface that remains kernel-owned and a
second ENA interface dedicated to DPDK. Never bind the SSH/SSM management
interface to `vfio-pci`.

1. Record AMI, kernel, DPDK, ENA firmware/driver, PCI IDs, NUMA topology, CPU
   model, hugepages, IOMMU groups, and interface mapping.
2. Prove the infrastructure first with `testpmd` and the ENA PMD.
3. Run the XNIC forwarder only after `testpmd` can RX/TX on the dedicated port.
4. Use a second traffic host and preserve offered load, frame sizes, queue/core
   mapping, xstats, packet captures, and loss.
5. Exercise partial TX, link interruption, SIGTERM, and repeated setup/teardown.
6. Stop, then terminate both instances immediately after copying evidence.

The pass statement is functional ENA-PMD execution. Throughput or latency may
be claimed only with a controlled peer, topology, repetitions, and raw results.

## Two-node EFA/RDMA gate

Both nodes must use a supported type in the same subnet and Availability Zone.
The EFA security group permits all traffic to and from itself. Install the AWS
EFA software stack with Libfabric, then require:

```sh
fi_info -p efa -t FI_EP_RDM
fi_pingpong -p efa <peer-private-address>
fi_rdm_bw -p efa <peer-private-address>
```

Record provider/domain/protocol output, CPU affinity, message sizes,
warmup/iteration counts, median/tail latency, bandwidth, errors, and ENA/EFA
counters. Run a TCP provider baseline under the same placement. EFA traffic
cannot cross Availability Zones, so an AZ mismatch is a failed deployment, not
an RDMA performance result.

This demonstrates Libfabric/EFA operation, not verbs-level driver development
or ConnectX/RoCE production experience.
