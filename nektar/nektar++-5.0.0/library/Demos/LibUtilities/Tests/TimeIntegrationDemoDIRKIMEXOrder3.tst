<?xml version="1.0" encoding="utf-8" ?>
<test>
    <description>Test for time integration schemes</description>
    <executable>TimeIntegrationDemo</executable>
    <parameters>--points 100 --timesteps 100 --method 6</parameters>
    <metrics>
        <metric type="L2" id="1">
            <value tolerance="1e-12">0.061634</value>
        </metric>
    </metrics>
</test>
