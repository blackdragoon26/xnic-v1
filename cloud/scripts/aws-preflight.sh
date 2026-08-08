#!/bin/sh
set -eu

export AWS_PAGER=""

command -v aws >/dev/null 2>&1 || {
	echo "AWS CLI is required" >&2
	exit 1
}
REGION=${AWS_REGION:-$(aws configure get region 2>/dev/null || true)}
AZ=${AWS_AVAILABILITY_ZONE:-}
INSTANCE_TYPE=${AWS_INSTANCE_TYPE:-}
if [ -z "$REGION" ]; then
	echo "set AWS_REGION or configure a default region" >&2
	exit 1
fi

echo "region=$REGION"
if ! aws sts get-caller-identity >/dev/null 2>&1; then
	echo "identity=invalid" >&2
	echo "repair the AWS credentials before any quota or cost decision" >&2
	exit 2
fi
echo "identity=valid"

echo "[availability zones]"
aws ec2 describe-availability-zones --region "$REGION" \
	--filters Name=state,Values=available \
	--query 'AvailabilityZones[*].[ZoneName,ZoneId]' --output table

echo "[regional instance-type metadata: EFA-supported]"
aws ec2 describe-instance-types --region "$REGION" \
	--filters Name=network-info.efa-supported,Values=true \
	--query 'InstanceTypes[*].InstanceType' --output text

echo "[relevant EC2 on-demand quotas]"
aws service-quotas list-service-quotas --region "$REGION" \
	--service-code ec2 \
	--query "Quotas[?contains(QuotaName, 'Running On-Demand')].[QuotaName,Value,QuotaCode]" \
	--output table

if [ -n "$AZ" ] && [ -n "$INSTANCE_TYPE" ]; then
	echo "[offering metadata for $INSTANCE_TYPE in $AZ]"
	aws ec2 describe-instance-type-offerings --region "$REGION" \
		--location-type availability-zone \
		--filters "Name=location,Values=$AZ" \
			"Name=instance-type,Values=$INSTANCE_TYPE" \
		--query 'InstanceTypeOfferings[*].[Location,InstanceType]' --output table
else
	echo "offering_check=skipped (set AWS_AVAILABILITY_ZONE and AWS_INSTANCE_TYPE)"
fi

echo "preflight=identity-quota-offering-metadata-pass"
echo "launch_capacity_validated=0"
echo "resources_created=0"
echo "Before launch, record an approved instance type, AZ, hourly price, runtime cap, and maximum spend."
