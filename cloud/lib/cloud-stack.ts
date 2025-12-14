import * as cdk from 'aws-cdk-lib';
import { Construct } from 'constructs';
import { ConstructsFactories } from '@aws-solutions-constructs/aws-constructs-factories';
import {
  aws_iam as iam,
  aws_iot as iot,
  aws_iotfleetwise as iotfleetwise,
  custom_resources as cr,
  aws_secretsmanager as secretsmanager,
} from 'aws-cdk-lib';
import { ThingWithCert } from 'cdk-iot-core-certificates-v3';
import * as fs from 'fs';

export class CloudStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    // ビジョンシステムデータ用S3バケット
    const factories = new ConstructsFactories(this, 'Factories');
    const response = factories.s3BucketFactory('VisionSystemDataBucket', {
      bucketProps: {
        bucketName: `vision-system-data-${this.account}-${this.region}`,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
        autoDeleteObjects: true,
      },
      loggingBucketProps: {
        bucketName: `vision-system-data-access-logs-${this.account}-${this.region}`,
        removalPolicy: cdk.RemovalPolicy.DESTROY,
        autoDeleteObjects: true,
      }
    });
    // バケットポリシー
    // https://github.com/aws/aws-iot-fleetwise-edge/blob/main/docs/dev-guide/vision-system-data/vision-system-data-demo.ipynb
    const bucket = response.s3Bucket;
    bucket.addToResourcePolicy(
      new iam.PolicyStatement({
        actions: ['s3:ListBucket'],
        resources: [bucket.bucketArn],
        principals: [new iam.ServicePrincipal('iotfleetwise.amazonaws.com')],
      })
    );
    bucket.addToResourcePolicy(
      new iam.PolicyStatement({
        actions: ['s3:PutObject', 's3:GetObject'],
        resources: [bucket.arnForObjects('*')],
        principals: [new iam.ServicePrincipal('iotfleetwise.amazonaws.com')],
      }),
    );
    new cdk.CfnOutput(this, 'VisionSystemDataBucketOutput', {
      exportName: 'VisionSystemDataBucket',
      value: bucket.bucketName,
    });

    // IoT Core 認証プロバイダー用IAM Role
    const iotCoreAuthRole = new iam.Role(this, 'CredentialsProviderRole', {
      assumedBy: new iam.ServicePrincipal('credentials.iot.amazonaws.com'),
      inlinePolicies: {
        'AllowS3Access': new iam.PolicyDocument({
          statements: [
            new iam.PolicyStatement({
              actions: ['s3:PutObject', 's3:PutObjectAcl', 's3:ListBucket'],
              resources: [response.s3Bucket.bucketArn, response.s3Bucket.arnForObjects('*')],
            }),
          ],
        }),
      },
    });

    // 認証情報用ロールエイリアス
    const alias = new iot.CfnRoleAlias(this, 'CredentialsProviderRoleAlias', {
      roleAlias: 'CredentialsProviderRoleAlias',
      roleArn: iotCoreAuthRole.roleArn,
    });

    new cdk.CfnOutput(this, 'CredentialsProviderRoleAliasOutput', {
      exportName: 'CredentialsProviderRoleAlias',
      value: alias.roleAlias!,
    });

    // IoT Core 証明書を作成
    const certCr = new cr.AwsCustomResource(this, 'IotCoreCertificateCr', {
      onCreate: {
        service: 'iot',
        action: 'CreateKeysAndCertificate',
        parameters: {
          setAsActive: true,
        },
        physicalResourceId: cr.PhysicalResourceId.of('IotCoreCertificateCr'),
      },
      policy: cr.AwsCustomResourcePolicy.fromSdkCalls({
        resources: cr.AwsCustomResourcePolicy.ANY_RESOURCE,
      }),
      installLatestAwsSdk: true,
    });
    const certificate = certCr.getResponseField('certificatePem');
    const privateKey = certCr.getResponseField('keyPair.PrivateKey');
    const certArn = certCr.getResponseField('certificateArn');

    
    // IoT Core Thingと証明書
    const thingName = 'vehicle-ros2-test';
    const thing = new ThingWithCert(this, 'IotCoreThing', {
      thingName: thingName,
    });
    new cdk.CfnOutput(this, 'VehicleNameOutput', {
      exportName: 'FleetWiseVehicleName',
      value: thingName,
    });

    // Thing と証明書を紐づけ
    new iot.CfnThingPrincipalAttachment(this, 'IotCoreThingPrincipalAttachment', {
      principal: certArn,
      thingName: thingName,
    });

    // IoT Core ポリシー
    // https://docs.aws.amazon.com/ja_jp/iot-fleetwise/latest/developerguide/provision-vehicles.html
    const policy = new iot.CfnPolicy(this, 'IotCorePolicy', {
      policyName: 'IotCorePolicy',
      policyDocument: {
        Version: '2012-10-17',
        Statement: [
          {
            Effect: iam.Effect.ALLOW,
            Action: ['iot:Connect'],
            Resource: `arn:aws:iot:${this.region}:${this.account}:client/\${iot:Connection.Thing.ThingName}`,
          },
          {
            Effect: iam.Effect.ALLOW,
            Action: ['iot:Publish'],
            Resource: [
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/iotfleetwise/vehicles/\${iot:Connection.Thing.ThingName}/checkins`,
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/iotfleetwise/vehicles/\${iot:Connection.Thing.ThingName}/signals`,
            ],
          },
          {
            Effect: iam.Effect.ALLOW,
            Action: ['iot:Subscribe'],
            Resource: [
              `arn:aws:iot:${this.region}:${this.account}:topicfilter/$aws/iotfleetwise/vehicles/\${iot:Connection.Thing.ThingName}/*`,
              `arn:aws:iot:${this.region}:${this.account}:topicfilter/$aws/things/\${iot:Connection.Thing.ThingName}/jobs/*`,
              `arn:aws:iot:${this.region}:${this.account}:topicfilter/$aws/events/job/*`,
              `arn:aws:iot:${this.region}:${this.account}:topicfilter/$aws/commands/things/\${iot:Connection.Thing.ThingName}/executions/*`,
            ],
          },
          {
            Effect: iam.Effect.ALLOW,
            Action: ['iot:Receive'],
            Resource: [
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/iotfleetwise/vehicles/\${iot:Connection.Thing.ThingName}/*`,
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/things/\${iot:Connection.Thing.ThingName}/jobs/*`,
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/events/job/*`,
              `arn:aws:iot:${this.region}:${this.account}:topic/$aws/commands/things/\${iot:Connection.Thing.ThingName}/executions/*`,
            ],
          },
          {
            Effect: iam.Effect.ALLOW,
            Action: ['s3:PutObject', 's3:PutObjectAcl', 's3:ListBucket'],
            Resource: [bucket.bucketArn, bucket.arnForObjects('*')],
          },
          {
            Effect: iam.Effect.ALLOW,
            Action: ['iot:AssumeRoleWithCertificate'],
            Resource: `arn:aws:iot:${this.region}:${this.account}:rolealias/${alias.roleAlias}`,
          }
        ]
      }
    });

    // ポリシーを証明書に紐づけ
    const policyPrincipalAttachment = new iot.CfnPolicyPrincipalAttachment(this, 'IotCorePolicyPrincipalAttachment', {
      policyName: policy.policyName!,
      principal: certArn,
    });
    policyPrincipalAttachment.node.addDependency(certCr);

    // 証明書をSecrets Managerに保存
    const secret = new secretsmanager.Secret(this, 'IotCoreCertificateSecret', {
      secretName: 'vehicle-ros2-test-certificate',
      secretObjectValue: {
        privateKey: cdk.SecretValue.unsafePlainText(privateKey),
        certificate: cdk.SecretValue.unsafePlainText(certificate),
      },
    });
    secret.node.addDependency(thing);
    secret.node.addDependency(certCr);
    new cdk.CfnOutput(this, 'IotCoreCertificateSecretOutput', {
      exportName: 'IotCoreCertificateSecret',
      value: secret.secretName,
    });

    // シグナルカタログ(ROS2にCfnが未対応なのでCustomResourceで作成)
    // ※ NodeにStructとPropertyが無い
    // https://docs.aws.amazon.com/AWSCloudFormation/latest/TemplateReference/aws-properties-iotfleetwise-signalcatalog-node.html
    // ros2-nodes.jsonを読み込む
    const nodes = JSON.parse(fs.readFileSync('lib/ros2-nodes.json', 'utf8'));

    const signalCatalogCr = new cr.AwsCustomResource(this, 'SignalCatalogCr', {
      onCreate: {
        service: 'iotfleetwise',
        action: 'CreateSignalCatalog',
        parameters: {
            name: 'DefaultSignalCatalog',
            nodes: nodes,
        },
        physicalResourceId: cr.PhysicalResourceId.of('ros2-signal-catalog'),
      },
      onDelete: {
        service: 'iotfleetwise',
        action: 'DeleteSignalCatalog',
        parameters: {
          name: 'DefaultSignalCatalog',
        },
        physicalResourceId: cr.PhysicalResourceId.of('ros2-signal-catalog'),
      },
      policy: cr.AwsCustomResourcePolicy.fromSdkCalls({
        resources: cr.AwsCustomResourcePolicy.ANY_RESOURCE,
      }),
      installLatestAwsSdk: true,
    });
    const signalCatalogArn = signalCatalogCr.getResponseField('arn');

    const modelManifest = new iotfleetwise.CfnModelManifest(this, 'ModelManifest', {
      name: 'Ros2ModelManifest',
      description: 'Model Manifest for ROS2',
      signalCatalogArn: signalCatalogArn,
      status: 'ACTIVE',
      nodes: [
        "Vehicle.Cameras.Front.Image",
        "Vehicle.Speed",
        "Vehicle.Temperature",
        "Vehicle.Acceleration",
      ]
    });
    modelManifest.node.addDependency(signalCatalogCr);

    // network-interfaces.jsonを読み込む
    const networkInterfaces = JSON.parse(fs.readFileSync('lib/network-interfaces.json', 'utf8'));
    // ros2-decoders.jsonを読み込む
    const decoders = JSON.parse(fs.readFileSync('lib/ros2-decoders.json', 'utf8'));
    const decoderManifestParams = {
      modelManifestArn: modelManifest.attrArn,
      name: 'Ros2DecoderManifest',
      description: 'Decoder Manifest for ROS2',
      networkInterfaces: networkInterfaces,
      signalDecoders: decoders,
      status: 'ACTIVE',
    }
    const decoderManifestCr = new cr.AwsCustomResource(this, 'DecoderManifestCr', {
      onCreate: {
        service: 'iotfleetwise',
        action: 'CreateDecoderManifest',
        parameters: decoderManifestParams,
        physicalResourceId: cr.PhysicalResourceId.of('ros2-decoder-manifest'),
      },
      onDelete: {
        service: 'iotfleetwise',
        action: 'DeleteDecoderManifest',
        parameters: {
          name: 'Ros2DecoderManifest',
        },
        physicalResourceId: cr.PhysicalResourceId.of('ros2-decoder-manifest'),
      },
      policy: cr.AwsCustomResourcePolicy.fromStatements([
        new iam.PolicyStatement({
          actions: ['iotfleetwise:CreateDecoderManifest', 'iotfleetwise:DeleteDecoderManifest'],
          resources: ['*'],
        }),
      ]),
      installLatestAwsSdk: true,
    });
    decoderManifestCr.node.addDependency(modelManifest);
    const decoderManifestArn = decoderManifestCr.getResponseField('arn');

    // デコーダーマニフェストのステータスをACTIVEにする
    const decoderManifestStatusCr = new cr.AwsCustomResource(this, 'DecoderManifestStatusCr', {
      onCreate: {
        service: 'iotfleetwise',
        action: 'UpdateDecoderManifest',
        parameters: {
          name: 'Ros2DecoderManifest',
          status: 'ACTIVE',
        },
        physicalResourceId: cr.PhysicalResourceId.of('ros2-decoder-manifest-status'),
      },
      policy: cr.AwsCustomResourcePolicy.fromStatements([
        new iam.PolicyStatement({
          actions: ['iotfleetwise:UpdateDecoderManifest'],
          resources: ['*'],
        }),
      ]),
      installLatestAwsSdk: true,
    });
    decoderManifestStatusCr.node.addDependency(decoderManifestCr);

    // 車両を作成
    const vehicle = new iotfleetwise.CfnVehicle(this, 'Vehicle', {
      name: thingName,
      modelManifestArn: modelManifest.attrArn,
      decoderManifestArn: decoderManifestArn,
      associationBehavior: 'ValidateIotThingExists',
    });
    vehicle.node.addDependency(decoderManifestStatusCr);
    vehicle.node.addDependency(modelManifest);

    // キャンペーンを作成
    const campaign = new iotfleetwise.CfnCampaign(this, 'Campaign', {
      name: 'Ros2Campaign',
      description: 'Campaign for ROS2',
      action: 'APPROVE',
      signalCatalogArn: signalCatalogArn,
      targetArn: vehicle.attrArn,
      // startTime: new Date().toISOString(),
      collectionScheme: {
        timeBasedCollectionScheme: {
          periodMs: 10000,          
        },
      },
      compression: 'SNAPPY',
      dataDestinationConfigs: [
        {
          s3Config: {
            bucketArn: bucket.bucketArn,
          },
        },
      ],
      signalsToCollect: [
        {
          name: 'Vehicle.Cameras.Front.Image',
        },
        {
          name: 'Vehicle.Speed',
        },
        {
          name: 'Vehicle.Acceleration',
        },
      ],
    });
  }
}
