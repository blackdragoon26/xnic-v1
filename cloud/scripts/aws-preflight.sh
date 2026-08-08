#!/bin/sh
set -eu

REGION=${AWS_REGION:-$(aws configure get region 2>/dev/null || true)}

command -v aws >/dev/null 2>&1 || {
	echo "AWS CLI is required" >&2
	exit 1
}
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

echo "[EFA-supported instance types in region]"
aws ec2 describe-instance-types --region "$REGION" \
	--filters Name=network-info.efa-supported,Values=true \
	--query 'InstanceTypes[*].InstanceType' --output text

echo "[relevant EC2 on-demand quotas]"
aws service-quotas list-service-quotas --region "$REGION" \
	--service-code ec2 \
	--query "Quotas[?contains(QuotaName, 'Running On-Demand')].[QuotaName,Value,QuotaCode]" \
	--output table

echo "preflight=read-only-pass"
echo "resources_created=0"
echo "Before launch, record an approved instance type, AZ, hourly price, runtime cap, and maximum spend."
