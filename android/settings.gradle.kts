pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    // 用户全局 init.gradle 注入了国内镜像仓库（阿里云等），PREFER_PROJECT 让镜像优先，
    // init 脚本自带 google()/mavenCentral() 兜底。
    repositoriesMode.set(RepositoriesMode.PREFER_PROJECT)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "OLY"
include(":app")
